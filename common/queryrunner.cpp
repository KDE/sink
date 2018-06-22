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
#include <thread>
#include <chrono>

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


template <class DomainType>
void QueryRunner<DomainType>::delayNextQuery()
{
    mDelayNextQuery = true;
}

//This function triggers the initial fetch, and then subsequent calls will simply fetch more data of mBatchSize.
template <class DomainType>
void QueryRunner<DomainType>::fetch(const Sink::Query &query, const QByteArray &bufferType)
{
    SinkTraceCtx(mLogCtx) << "Running fetcher. Batchsize: " << mBatchSize;
    if (mQueryInProgress) {
        SinkTraceCtx(mLogCtx) << "Query is already in progress, postponing: " << mBatchSize;
        mRequestFetchMore = true;
        return;
    }
    mQueryInProgress = true;
    bool addDelay = mDelayNextQuery;
    mDelayNextQuery = false;
    const bool runAsync = !query.synchronousQuery();
    //The lambda will be executed in a separate thread, so copy all arguments
    async::run<ReplayResult>([query,
                              bufferType,
                              resultProvider = mResultProvider,
                              resourceContext = mResourceContext,
                              logCtx = mLogCtx,
                              state = mQueryState,
                              resultTransformation = mResultTransformation,
                              batchSize = mBatchSize,
                              addDelay]() {
        QueryWorker<DomainType> worker(query, resourceContext, bufferType, resultTransformation, logCtx);
        const auto result =  worker.executeInitialQuery(query, *resultProvider, batchSize, state);

        //For testing only
        if (addDelay) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return result;
    }, runAsync)
        .then([this, query, bufferType, guardPtr = QPointer<QObject>(&guard)](const ReplayResult &result) {
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
            mResultProvider->setRevision(result.newRevision);
            mResultProvider->initialResultSetComplete(result.replayedAll);
            if (mRequestFetchMore) {
                mRequestFetchMore = false;
                //This code exists for incemental fetches, so we don't skip loading another set.
                fetch(query, bufferType);
                return;
            }
            if (mRevisionChangedMeanwhile) {
                incrementalFetch(query, bufferType).exec();
            }
        })
        .exec();
}

template <class DomainType>
KAsync::Job<void> QueryRunner<DomainType>::incrementalFetch(const Sink::Query &query, const QByteArray &bufferType)
{
    if (!mInitialQueryComplete && !mQueryInProgress) {
        //We rely on this codepath in the case of newly added resources to trigger the initial fetch.
        fetch(query, bufferType);
        return KAsync::null();
    }
    if (mQueryInProgress) {
        //If a query is already in progress we just remember to fetch again once the current query is done.
        mRevisionChangedMeanwhile = true;
        return KAsync::null();
    }
    mRevisionChangedMeanwhile = false;
    Q_ASSERT(!mQueryInProgress);
    bool addDelay = mDelayNextQuery;
    mDelayNextQuery = false;
    return KAsync::start([&] {
            mQueryInProgress = true;
        })
        //The lambda will be executed in a separate thread, so copy all arguments
        .then(async::run<ReplayResult>([query,
                                        bufferType,
                                        resultProvider = mResultProvider,
                                        resourceContext = mResourceContext,
                                        logCtx = mLogCtx,
                                        state = mQueryState,
                                        resultTransformation = mResultTransformation,
                                        addDelay]() {
                QueryWorker<DomainType> worker(query, resourceContext, bufferType, resultTransformation, logCtx);
                const auto result = worker.executeIncrementalQuery(query, *resultProvider, state);
                ////For testing only
                if (addDelay) {
                    SinkWarning() << "Sleeping in incremental query";
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                return result;
            }))
        .then([this, query, bufferType, guardPtr = QPointer<QObject>(&guard)](const ReplayResult &newRevisionAndReplayedEntities) {
            if (!guardPtr) {
                //Not an error, the query can vanish at any time.
                return KAsync::null();
            }
            mQueryInProgress = false;
            mResourceAccess->sendRevisionReplayedCommand(newRevisionAndReplayedEntities.newRevision);
            mResultProvider->setRevision(newRevisionAndReplayedEntities.newRevision);
            if (mRevisionChangedMeanwhile) {
                return incrementalFetch(query, bufferType);
            }
            return KAsync::null();
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

static QString operationName(Sink::Operation operation)
{
    switch (operation) {
        case Sink::Operation_Creation:
            return "Creation";
        case Sink::Operation_Modification:
            return "Modification";
        case Sink::Operation_Removal:
            return "Removal";
    }
    return "Unknown Operation";
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
    SinkTraceCtx(mLogCtx) << "Replaying: " << operationName(result.operation) << "\n" <<*valueCopy;
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

    auto entityStore = EntityStore{mResourceContext, mLogCtx};
    const qint64 topRevision = entityStore.maxRevision();
    SinkTraceCtx(mLogCtx) << "Running query update from revision: " << baseRevision << " to revision " << topRevision;
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
    SinkTraceCtx(mLogCtx) << "Replayed " << replayResult.replayedEntities << " results until revision: " << topRevision << "\n"
        << (replayResult.replayedAll ? "Replayed all available results.\n" : "")
        << "Incremental query took: " << Log::TraceTime(time.elapsed());
    return {topRevision, replayResult.replayedEntities, false, preparedQuery.getState()};
}

template <class DomainType>
ReplayResult QueryWorker<DomainType>::executeInitialQuery(
    const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, int batchsize, DataStoreQuery::State::Ptr state)
{
    QTime time;
    time.start();

    auto entityStore = EntityStore{mResourceContext, mLogCtx};
    const qint64 topRevision = entityStore.maxRevision();
    SinkTraceCtx(mLogCtx) << "Running query from revision: " << topRevision;
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

    return {topRevision, replayResult.replayedEntities, replayResult.replayedAll, preparedQuery.getState()};
}

#define REGISTER_TYPE(T) \
    template class QueryRunner<T>; \
    template class QueryWorker<T>; \

SINK_REGISTER_TYPES()
