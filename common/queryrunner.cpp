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

SINK_DEBUG_AREA("queryrunner")

using namespace Sink;
using namespace Sink::Storage;

struct ReplayResult {
    qint64 newRevision;
    qint64 replayedEntities;
    bool replayedAll;
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
    typedef std::function<bool(const typename DomainType::Ptr &domainObject, Sink::Operation operation, const QMap<QByteArray, QVariant> &aggregateValues)> ResultCallback;
public:
    QueryWorker(const Sink::Query &query, const ResourceContext &context, const QByteArray &bufferType, const QueryRunnerBase::ResultTransformation &transformation, const Sink::Log::Context &logCtx);
    virtual ~QueryWorker();

    ReplayResult executeIncrementalQuery(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);
    ReplayResult executeInitialQuery(const Sink::Query &query, const typename DomainType::Ptr &parent, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, int offset, int batchsize);

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
        SinkWarning() << "A limited query without sorting is typically a bad idea, because there is no telling what you're going to get.";
    }
    auto guardPtr = QPointer<QObject>(&guard);
    // We delegate loading of initial data to the result provider, so it can decide for itself what it needs to load.
    mResultProvider->setFetcher([=](const typename DomainType::Ptr &parent) {
        const QByteArray parentId = parent ? parent->identifier() : QByteArray();
        SinkTraceCtx(mLogCtx) << "Running fetcher. Offset: " << mOffset[parentId] << " Batchsize: " << mBatchSize;
        auto resultProvider = mResultProvider;
        if (query.synchronousQuery()) {
            QueryWorker<DomainType> worker(query, mResourceContext, bufferType, mResultTransformation, mLogCtx);
            const auto newRevisionAndReplayedEntities = worker.executeInitialQuery(query, parent, *resultProvider, mOffset[parentId], mBatchSize);
            mOffset[parentId] += newRevisionAndReplayedEntities.replayedEntities;
            resultProvider->setRevision(newRevisionAndReplayedEntities.newRevision);
            resultProvider->initialResultSetComplete(parent, newRevisionAndReplayedEntities.replayedAll);
        } else {
            auto resultTransformation = mResultTransformation;
            auto offset = mOffset[parentId];
            auto batchSize = mBatchSize;
            auto resourceContext = mResourceContext;
            auto logCtx = mLogCtx;
            //The lambda will be executed in a separate thread, so copy all arguments
            async::run<ReplayResult>([resultTransformation, offset, batchSize, query, bufferType, resourceContext, resultProvider, parent, logCtx]() {
                QueryWorker<DomainType> worker(query, resourceContext, bufferType, resultTransformation, logCtx);
                const auto  newRevisionAndReplayedEntities = worker.executeInitialQuery(query, parent, *resultProvider, offset, batchSize);
                return newRevisionAndReplayedEntities;
            })
                .template syncThen<void, ReplayResult>([this, parentId, query, parent, resultProvider, guardPtr](const ReplayResult &newRevisionAndReplayedEntities) {
                    if (!guardPtr) {
                        qWarning() << "The parent object is already gone";
                        return;
                    }
                    mOffset[parentId] += newRevisionAndReplayedEntities.replayedEntities;
                    // Only send the revision replayed information if we're connected to the resource, there's no need to start the resource otherwise.
                    if (query.liveQuery()) {
                        mResourceAccess->sendRevisionReplayedCommand(newRevisionAndReplayedEntities.newRevision);
                    }
                    resultProvider->setRevision(newRevisionAndReplayedEntities.newRevision);
                    resultProvider->initialResultSetComplete(parent, newRevisionAndReplayedEntities.replayedAll);
                })
                .exec();
        }
    });

    // In case of a live query we keep the runner for as long alive as the result provider exists
    if (query.liveQuery()) {
        Q_ASSERT(!query.synchronousQuery());
        // Incremental updates are always loaded directly, leaving it up to the result to discard the changes if they are not interesting
        setQuery([=]() -> KAsync::Job<void> {
            auto resultProvider = mResultProvider;
            auto resourceContext = mResourceContext;
            auto logCtx = mLogCtx;
            return async::run<ReplayResult>([=]() {
                       QueryWorker<DomainType> worker(query, resourceContext, bufferType, mResultTransformation, logCtx);
                       const auto newRevisionAndReplayedEntities = worker.executeIncrementalQuery(query, *resultProvider);
                       return newRevisionAndReplayedEntities;
                   })
                .template syncThen<void, ReplayResult>([query, this, resultProvider, guardPtr](const ReplayResult &newRevisionAndReplayedEntities) {
                    if (!guardPtr) {
                        qWarning() << "The parent object is already gone";
                        return;
                    }
                    // Only send the revision replayed information if we're connected to the resource, there's no need to start the resource otherwise.
                    mResourceAccess->sendRevisionReplayedCommand(newRevisionAndReplayedEntities.newRevision);
                    resultProvider->setRevision(newRevisionAndReplayedEntities.newRevision);
                });
        });
        // Ensure the connection is open, if it wasn't already opened
        // TODO If we are not connected already, we have to check for the latest revision once connected, otherwise we could miss some updates
        mResourceAccess->open();
        QObject::connect(mResourceAccess.data(), &Sink::ResourceAccess::revisionChanged, this, &QueryRunner::revisionChanged);
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
ReplayResult QueryWorker<DomainType>::executeIncrementalQuery(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider)
{
    QTime time;
    time.start();

    const qint64 baseRevision = resultProvider.revision() + 1;
    auto entityStore = EntityStore{mResourceContext, mLogCtx};
    auto preparedQuery = DataStoreQuery{query, ApplicationDomain::getTypeName<DomainType>(), entityStore};
    auto resultSet = preparedQuery.update(baseRevision);
    SinkTraceCtx(mLogCtx) << "Filtered set retrieved. " << Log::TraceTime(time.elapsed());
    auto replayResult = resultSet.replaySet(0, 0, [this, query, &resultProvider](const ResultSet::Result &result) {
        resultProviderCallback(query, resultProvider, result);
    });

    SinkTraceCtx(mLogCtx) << "Incremental query took: " << Log::TraceTime(time.elapsed());
    return {entityStore.maxRevision(), replayResult.replayedEntities, replayResult.replayedAll};
}

template <class DomainType>
ReplayResult QueryWorker<DomainType>::executeInitialQuery(
    const Sink::Query &query, const typename DomainType::Ptr &parent, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, int offset, int batchsize)
{
    QTime time;
    time.start();

    auto modifiedQuery = query;
    if (!query.parentProperty().isEmpty()) {
        if (parent) {
            SinkTraceCtx(mLogCtx) << "Running initial query for parent:" << parent->identifier();
            modifiedQuery.filter(query.parentProperty(), Query::Comparator(QVariant::fromValue(Sink::ApplicationDomain::Reference{parent->identifier()})));
        } else {
            SinkTraceCtx(mLogCtx) << "Running initial query for toplevel";
            modifiedQuery.filter(query.parentProperty(), Query::Comparator(QVariant{}));
        }
    }

    auto entityStore = EntityStore{mResourceContext, mLogCtx};
    auto preparedQuery = DataStoreQuery{modifiedQuery, ApplicationDomain::getTypeName<DomainType>(), entityStore};
    auto resultSet = preparedQuery.execute();

    SinkTraceCtx(mLogCtx) << "Filtered set retrieved. " << Log::TraceTime(time.elapsed());
    auto replayResult = resultSet.replaySet(offset, batchsize, [this, query, &resultProvider](const ResultSet::Result &result) {
        resultProviderCallback(query, resultProvider, result);
    });

    SinkTraceCtx(mLogCtx) << "Initial query took: " << Log::TraceTime(time.elapsed());
    return {entityStore.maxRevision(), replayResult.replayedEntities, replayResult.replayedAll};
}

template class QueryRunner<Sink::ApplicationDomain::Contact>;
template class QueryRunner<Sink::ApplicationDomain::Folder>;
template class QueryRunner<Sink::ApplicationDomain::Mail>;
template class QueryRunner<Sink::ApplicationDomain::Event>;
template class QueryWorker<Sink::ApplicationDomain::Contact>;
template class QueryWorker<Sink::ApplicationDomain::Folder>;
template class QueryWorker<Sink::ApplicationDomain::Mail>;
template class QueryWorker<Sink::ApplicationDomain::Event>;
