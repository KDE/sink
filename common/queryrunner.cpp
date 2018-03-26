/*
    Copyright (c) 2015 Christian Mollekopf <mollekopf@kolabsys.com>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/
#include "queryrunner.h"

#include <limits>
#include <QTime>
#include <QPointer>

#include "commands.h"
#include "asyncutils.h"
#include "datastorequery.h"

using namespace Sink;
using namespace Sink::Storage;

struct ReplayResult {
    qint64 newRevision;
    qint64 replayedEntities;
    bool replayedAll;
    DataStoreQuery::State::Ptr queryState;
};

/*
 * This class wraps the actual query implementation.
 *
 * This is a worker object that can be moved to a thread to execute the query.
 * The only interaction point is the ResultProvider, which handles the threadsafe reporting of the result.
 */
template <typename DomainType>
class QueryWorker : public QObject
{
public:
    QueryWorker(const Sink::Query &query, const ResourceContext &context, const QByteArray &bufferType, const QueryRunnerBase::ResultTransformation &transformation, const Sink::Log::Context &logCtx);
    virtual ~QueryWorker();

    ReplayResult executeIncrementalQuery(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, DataStoreQuery::State::Ptr state);
    ReplayResult executeInitialQuery(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, int batchsize, DataStoreQuery::State::Ptr state);

private:
    void resultProviderCallback(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, const ResultSet::Result &result);

    QueryRunnerBase::ResultTransformation mResultTransformation;
    ResourceContext mResourceContext;
    Sink::Log::Context mLogCtx;
};

template <class DomainType>
QueryRunner<DomainType>::QueryRunner(const Sink::Query &query, const Sink::ResourceContext &context, const QByteArray &bufferType, const Sink::Log::Context &logCtx)
    : QueryRunnerBase(), mResourceContext(context), mResourceAccess(mResourceContext.resourceAccess()), mResultProvider(new ResultProvider<typename DomainType::Ptr>), mBatchSize(query.limit()), mLogCtx(logCtx.subContext("queryrunner"))
{
    SinkTraceCtx(mLogCtx) << "Starting query. Is live:" << query.liveQuery() << " Limit: " << query.limit();
    if (query.limit() && query.sortProperty().isEmpty()) {
        SinkWarningCtx(mLogCtx) << "A limited query without sorting is typically a bad idea, because there is no telling what you're going to get.";
    }
    // We delegate loading of initial data to the result provider, so it can decide for itself what it needs to load.
    mResultProvider->setFetcher([this, query, bufferType] { fetch(query, bufferType); });

    // In case of a live query we keep the runner for as long alive as the result provider exists
    if (query.liveQuery()) {
        Q_ASSERT(!query.synchronousQuery());
        // Incremental updates are always loaded directly, leaving it up to the result to discard the changes if they are not interesting
        setQuery([=]() { return incrementalFetch(query, bufferType); });
        // Ensure the connection is open, if it wasn't already opened
        mResourceAccess->open();
        QObject::connect(mResourceAccess.data(), &Sink::ResourceAccess::revisionChanged, this, &QueryRunner::revisionChanged);
        // open is not synchronous, so from the time when the initial query is started until we have started and connected to the resource, it's possible to miss updates. We therefore unconditionally try to fetch new entities once we are connected.
        QObject::connect(mResourceAccess.data(), &Sink::ResourceAccess::ready, this, [this] (bool ready) {
            if (ready) {
                revisionChanged();
            }
        });
    }
    mResultProvider->onDone([this]() {
        delete this;
    });
}

template <class DomainType>
QueryRunner<DomainType>::~QueryRunner()
{
    SinkTraceCtx(mLogCtx) << "Stopped query";
}

//This function triggers the initial fetch, and then subsequent calls will simply fetch more data of mBatchSize.
template <class DomainType>
void QueryRunner<DomainType>::fetch(const Sink::Query &query, const QByteArray &bufferType)
{
    auto guardPtr = QPointer<QObject>(&guard);
    SinkTraceCtx(mLogCtx) << "Running fetcher. Batchsize: " << mBatchSize;
    if (mQueryInProgress) {
        SinkTraceCtx(mLogCtx) << "Query is already in progress, postponing: " << mBatchSize;
        mRequestFetchMore = true;
        return;
    }
    mQueryInProgress = true;
    auto resultProvider = mResultProvider;
    auto resultTransformation = mResultTransformation;
    auto batchSize = mBatchSize;
    auto resourceContext = mResourceContext;
    auto logCtx = mLogCtx;
    auto state = mQueryState;
    const bool runAsync = !query.synchronousQuery();
    //The lambda will be executed in a separate thread, so copy all arguments
    async::run<ReplayResult>([=]() {
        QueryWorker<DomainType> worker(query, resourceContext, bufferType, resultTransformation, logCtx);
        return worker.executeInitialQuery(query, *resultProvider, batchSize, state);
    }, runAsync)
        .then([=](const ReplayResult &result) {
            if (!guardPtr) {
                //Not an error, the query can vanish at any time.
                return;
            }
            mInitialQueryComplete = true;
            mQueryInProgress = false;
            mQueryState = result.queryState;
            // Only send the revision replayed information if we're connected to the resource, there's no need to start the resource otherwise.
            if (query.liveQuery()) {
                mResourceAccess->sendRevisionReplayedCommand(result.newRevision);
            }
            resultProvider->setRevision(result.newRevision);
            resultProvider->initialResultSetComplete(result.replayedAll);
            if (mRequestFetchMore) {
                mRequestFetchMore = false;
                fetch(query, bufferType);
            }
        })
        .exec();
}

template <class DomainType>
KAsync::Job<void> QueryRunner<DomainType>::incrementalFetch(const Sink::Query &query, const QByteArray &bufferType)
{
    if (!mInitialQueryComplete) {
        SinkWarningCtx(mLogCtx) << "Can't start the incremental query before the initial query is complete";
        fetch(query, bufferType);
        return KAsync::null();
    }
    if (mQueryInProgress) {
        //Can happen if the revision come in quicker than we process them.
        return KAsync::null();
    }
    auto resultProvider = mResultProvider;
    auto resourceContext = mResourceContext;
    auto logCtx = mLogCtx;
    auto state = mQueryState;
    auto resultTransformation = mResultTransformation;
    Q_ASSERT(!mQueryInProgress);
    auto guardPtr = QPointer<QObject>(&guard);
    return KAsync::start([&] {
            mQueryInProgress = true;
        })
        .then(async::run<ReplayResult>([=]() {
                QueryWorker<DomainType> worker(query, resourceContext, bufferType, resultTransformation, logCtx);
                return worker.executeIncrementalQuery(query, *resultProvider, state);
            }))
        .then([query, this, resultProvider, guardPtr](const ReplayResult &newRevisionAndReplayedEntities) {
            if (!guardPtr) {
                //Not an error, the query can vanish at any time.
                return;
            }
            mQueryInProgress = false;
            // Only send the revision replayed information if we're connected to the resource, there's no need to start the resource otherwise.
            mResourceAccess->sendRevisionReplayedCommand(newRevisionAndReplayedEntities.newRevision);
            resultProvider->setRevision(newRevisionAndReplayedEntities.newRevision);
        });
}

template <class DomainType>
void QueryRunner<DomainType>::setResultTransformation(const ResultTransformation &transformation)
{
    mResultTransformation = transformation;
}

template <class DomainType>
typename Sink::ResultEmitter<typename DomainType::Ptr>::Ptr QueryRunner<DomainType>::emitter()
{
    return mResultProvider->emitter();
}

template <class DomainType>
QueryWorker<DomainType>::QueryWorker(const Sink::Query &query, const Sink::ResourceContext &resourceContext,
    const QByteArray &bufferType, const QueryRunnerBase::ResultTransformation &transformation, const Sink::Log::Context &logCtx)
    : QObject(), mResultTransformation(transformation), mResourceContext(resourceContext), mLogCtx(logCtx.subContext("worker"))
{
    SinkTraceCtx(mLogCtx) << "Starting query worker";
}

template <class DomainType>
QueryWorker<DomainType>::~QueryWorker()
{
    SinkTraceCtx(mLogCtx) << "Stopped query worker";
}

template <class DomainType>
void QueryWorker<DomainType>::resultProviderCallback(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, const ResultSet::Result &result)
{
    auto valueCopy = Sink::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<DomainType>(result.entity, query.requestedProperties).template staticCast<DomainType>();
    for (auto it = result.aggregateValues.constBegin(); it != result.aggregateValues.constEnd(); it++) {
        valueCopy->setProperty(it.key(), it.value());
    }
    valueCopy->aggregatedIds() = result.aggregateIds;
    if (mResultTransformation) {
        mResultTransformation(*valueCopy);
    }
    switch (result.operation) {
        case Sink::Operation_Creation:
            //SinkTraceCtx(mLogCtx) << "Got creation: " << valueCopy->identifier();
            resultProvider.add(valueCopy);
            break;
        case Sink::Operation_Modification:
            //SinkTraceCtx(mLogCtx) << "Got modification: " << valueCopy->identifier();
            resultProvider.modify(valueCopy);
            break;
        case Sink::Operation_Removal:
            //SinkTraceCtx(mLogCtx) << "Got removal: " << valueCopy->identifier();
            resultProvider.remove(valueCopy);
            break;
    }
}

template <class DomainType>
ReplayResult QueryWorker<DomainType>::executeIncrementalQuery(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, DataStoreQuery::State::Ptr state)
{
    QTime time;
    time.start();

    const qint64 baseRevision = resultProvider.revision() + 1;
    SinkTraceCtx(mLogCtx) << "Running query update from revision: " << baseRevision;

    auto entityStore = EntityStore{mResourceContext, mLogCtx};
    if (!state) {
        SinkWarningCtx(mLogCtx) << "No previous query state.";
        return {0, 0, false, DataStoreQuery::State::Ptr{}};
    }
    auto preparedQuery = DataStoreQuery{*state, ApplicationDomain::getTypeName<DomainType>(), entityStore, true};
    auto resultSet = preparedQuery.update(baseRevision);
    SinkTraceCtx(mLogCtx) << "Filtered set retrieved. " << Log::TraceTime(time.elapsed());
    auto replayResult = resultSet.replaySet(0, 0, [this, query, &resultProvider](const ResultSet::Result &result) {
        resultProviderCallback(query, resultProvider, result);
    });
    preparedQuery.updateComplete();
    SinkTraceCtx(mLogCtx) << "Replayed " << replayResult.replayedEntities << " results.\n"
        << (replayResult.replayedAll ? "Replayed all available results.\n" : "")
        << "Incremental query took: " << Log::TraceTime(time.elapsed());
    return {entityStore.maxRevision(), replayResult.replayedEntities, false, preparedQuery.getState()};
}

template <class DomainType>
ReplayResult QueryWorker<DomainType>::executeInitialQuery(
    const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, int batchsize, DataStoreQuery::State::Ptr state)
{
    QTime time;
    time.start();

    auto entityStore = EntityStore{mResourceContext, mLogCtx};
    auto preparedQuery = [&] {
        if (state) {
            return DataStoreQuery{*state, ApplicationDomain::getTypeName<DomainType>(), entityStore, false};
        } else {
            return DataStoreQuery{query, ApplicationDomain::getTypeName<DomainType>(), entityStore};
        }
    }();
    auto resultSet = preparedQuery.execute();

    SinkTraceCtx(mLogCtx) << "Filtered set retrieved." << Log::TraceTime(time.elapsed());
    auto replayResult = resultSet.replaySet(0, batchsize, [this, query, &resultProvider](const ResultSet::Result &result) {
        resultProviderCallback(query, resultProvider, result);
    });

    SinkTraceCtx(mLogCtx) << "Replayed " << replayResult.replayedEntities << " results.\n"
        << (replayResult.replayedAll ? "Replayed all available results.\n" : "")
        << "Initial query took: " << Log::TraceTime(time.elapsed());

    return {entityStore.maxRevision(), replayResult.replayedEntities, replayResult.replayedAll, preparedQuery.getState()};
}

#define REGISTER_TYPE(T) \
    template class QueryRunner<T>; \
    template class QueryWorker<T>; \

SINK_REGISTER_TYPES()
