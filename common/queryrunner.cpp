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
#include "log.h"
#include "storage.h"
#include "definitions.h"
#include "domainadaptor.h"
#include "asyncutils.h"
#include "entityreader.h"

SINK_DEBUG_AREA("queryrunner")

using namespace Sink;

/*
 * This class wraps the actual query implementation.
 *
 * This is a worker object that can be moved to a thread to execute the query.
 * The only interaction point is the ResultProvider, which handles the threadsafe reporting of the result.
 */
template <typename DomainType>
class QueryWorker : public QObject
{
    // SINK_DEBUG_COMPONENT(mResourceInstanceIdentifier, mId)
    SINK_DEBUG_COMPONENT(mResourceInstanceIdentifier)
public:
    QueryWorker(const Sink::Query &query, const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &, const QByteArray &bufferType,
        const QueryRunnerBase::ResultTransformation &transformation);
    virtual ~QueryWorker();

    QPair<qint64, qint64> executeIncrementalQuery(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);
    QPair<qint64, qint64> executeInitialQuery(const Sink::Query &query, const typename DomainType::Ptr &parent, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, int offset, int batchsize);

private:
    Storage::Transaction getTransaction();
    std::function<bool(const typename DomainType::Ptr &, Sink::Operation)> resultProviderCallback(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);

    QueryRunnerBase::ResultTransformation mResultTransformation;
    DomainTypeAdaptorFactoryInterface::Ptr mDomainTypeAdaptorFactory;
    QByteArray mResourceInstanceIdentifier;
    QByteArray mId; //Used for identification in debug output
};

template <class DomainType>
QueryRunner<DomainType>::QueryRunner(const Sink::Query &query, const Sink::ResourceAccessInterface::Ptr &resourceAccess, const QByteArray &instanceIdentifier,
    const DomainTypeAdaptorFactoryInterface::Ptr &factory, const QByteArray &bufferType)
    : QueryRunnerBase(), mResourceAccess(resourceAccess), mResultProvider(new ResultProvider<typename DomainType::Ptr>), mBatchSize(query.limit)
{
    SinkTrace() << "Starting query";
    if (query.limit && query.sortProperty.isEmpty()) {
        SinkWarning() << "A limited query without sorting is typically a bad idea.";
    }
    auto guardPtr = QPointer<QObject>(&guard);
    // We delegate loading of initial data to the result provider, so it can decide for itself what it needs to load.
    mResultProvider->setFetcher([=](const typename DomainType::Ptr &parent) {
        const QByteArray parentId = parent ? parent->identifier() : QByteArray();
        SinkTrace() << "Running fetcher. Offset: " << mOffset[parentId] << " Batchsize: " << mBatchSize;
        auto resultProvider = mResultProvider;
        if (query.synchronousQuery) {
            QueryWorker<DomainType> worker(query, instanceIdentifier, factory, bufferType, mResultTransformation);
            worker.executeInitialQuery(query, parent, *resultProvider, mOffset[parentId], mBatchSize);
            resultProvider->initialResultSetComplete(parent);
        } else {
            auto resultTransformation = mResultTransformation;
            auto offset = mOffset[parentId];
            auto batchSize = mBatchSize;
            //The lambda will be executed in a separate thread, so we're extra careful
            async::run<QPair<qint64, qint64> >([resultTransformation, offset, batchSize, query, bufferType, instanceIdentifier, factory, resultProvider, parent]() {
                QueryWorker<DomainType> worker(query, instanceIdentifier, factory, bufferType, resultTransformation);
                const auto  newRevisionAndReplayedEntities = worker.executeInitialQuery(query, parent, *resultProvider, offset, batchSize);
                return newRevisionAndReplayedEntities;
            })
                .template syncThen<void, QPair<qint64, qint64>>([this, parentId, query, parent, resultProvider, guardPtr](const QPair<qint64, qint64> &newRevisionAndReplayedEntities) {
                    if (!guardPtr) {
                        qWarning() << "The parent object is already gone";
                        return;
                    }
                    mOffset[parentId] += newRevisionAndReplayedEntities.second;
                    // Only send the revision replayed information if we're connected to the resource, there's no need to start the resource otherwise.
                    if (query.liveQuery) {
                        mResourceAccess->sendRevisionReplayedCommand(newRevisionAndReplayedEntities.first);
                    }
                    resultProvider->setRevision(newRevisionAndReplayedEntities.first);
                    resultProvider->initialResultSetComplete(parent);
                })
                .exec();
        }
    });

    // In case of a live query we keep the runner for as long alive as the result provider exists
    if (query.liveQuery) {
        Q_ASSERT(!query.synchronousQuery);
        // Incremental updates are always loaded directly, leaving it up to the result to discard the changes if they are not interesting
        setQuery([=]() -> KAsync::Job<void> {
            auto resultProvider = mResultProvider;
            return async::run<QPair<qint64, qint64> >([=]() {
                       QueryWorker<DomainType> worker(query, instanceIdentifier, factory, bufferType, mResultTransformation);
                       const auto newRevisionAndReplayedEntities = worker.executeIncrementalQuery(query, *resultProvider);
                       return newRevisionAndReplayedEntities;
                   })
                .template syncThen<void, QPair<qint64, qint64> >([query, this, resultProvider, guardPtr](const QPair<qint64, qint64> &newRevisionAndReplayedEntities) {
                    if (!guardPtr) {
                        qWarning() << "The parent object is already gone";
                        return;
                    }
                    // Only send the revision replayed information if we're connected to the resource, there's no need to start the resource otherwise.
                    mResourceAccess->sendRevisionReplayedCommand(newRevisionAndReplayedEntities.first);
                    resultProvider->setRevision(newRevisionAndReplayedEntities.first);
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
    SinkTrace() << "Stopped query";
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
QueryWorker<DomainType>::QueryWorker(const Sink::Query &query, const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &factory,
    const QByteArray &bufferType, const QueryRunnerBase::ResultTransformation &transformation)
    : QObject(), mResultTransformation(transformation), mDomainTypeAdaptorFactory(factory), mResourceInstanceIdentifier(instanceIdentifier), mId(QUuid::createUuid().toByteArray())
{
    SinkTrace() << "Starting query worker";
}

template <class DomainType>
QueryWorker<DomainType>::~QueryWorker()
{
    SinkTrace() << "Stopped query worker";
}

template <class DomainType>
std::function<bool(const typename DomainType::Ptr &, Sink::Operation)> QueryWorker<DomainType>::resultProviderCallback(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider)
{
    return [this, &query, &resultProvider](const typename DomainType::Ptr &domainObject, Sink::Operation operation) -> bool {
        auto valueCopy = Sink::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<DomainType>(*domainObject, query.requestedProperties).template staticCast<DomainType>();
        if (mResultTransformation) {
            mResultTransformation(*valueCopy);
        }
        switch (operation) {
            case Sink::Operation_Creation:
                // SinkTrace() << "Got creation";
                resultProvider.add(valueCopy);
                break;
            case Sink::Operation_Modification:
                // SinkTrace() << "Got modification";
                resultProvider.modify(valueCopy);
                break;
            case Sink::Operation_Removal:
                // SinkTrace() << "Got removal";
                resultProvider.remove(valueCopy);
                break;
        }
        return true;
    };
}

template <class DomainType>
QPair<qint64, qint64> QueryWorker<DomainType>::executeIncrementalQuery(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider)
{
    QTime time;
    time.start();

    auto transaction = getTransaction();

    Sink::EntityReader<DomainType> reader(*mDomainTypeAdaptorFactory, mResourceInstanceIdentifier, transaction);
    auto revisionAndReplayedEntities = reader.executeIncrementalQuery(query, resultProvider.revision(), resultProviderCallback(query, resultProvider));
    SinkTrace() << "Incremental query took: " << Log::TraceTime(time.elapsed());
    return revisionAndReplayedEntities;
}

template <class DomainType>
Storage::Transaction QueryWorker<DomainType>::getTransaction()
{
    Sink::Storage::Transaction transaction;
    {
        Sink::Storage storage(Sink::storageLocation(), mResourceInstanceIdentifier);
        if (!storage.exists()) {
            //This is not an error if the resource wasn't started before
            SinkLog() << "Store doesn't exist: " << mResourceInstanceIdentifier;
            return Sink::Storage::Transaction();
        }
        storage.setDefaultErrorHandler([this](const Sink::Storage::Error &error) { SinkWarning() << "Error during query: " << error.store << error.message; });
        transaction = storage.createTransaction(Sink::Storage::ReadOnly);
    }

    //FIXME this is a temporary measure to recover from a failure to open the named databases correctly.
    //Once the actual problem is fixed it will be enough to simply crash if we open the wrong database (which we check in openDatabase already).
    while (!transaction.validateNamedDatabases()) {
        Sink::Storage storage(Sink::storageLocation(), mResourceInstanceIdentifier);
        transaction = storage.createTransaction(Sink::Storage::ReadOnly);
    }
    return transaction;
}

template <class DomainType>
QPair<qint64, qint64> QueryWorker<DomainType>::executeInitialQuery(
    const Sink::Query &query, const typename DomainType::Ptr &parent, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, int offset, int batchsize)
{
    QTime time;
    time.start();

    auto modifiedQuery = query;
    if (!query.parentProperty.isEmpty()) {
        if (parent) {
            SinkTrace() << "Running initial query for parent:" << parent->identifier();
            modifiedQuery.propertyFilter.insert(query.parentProperty, Query::Comparator(parent->identifier()));
        } else {
            SinkTrace() << "Running initial query for toplevel";
            modifiedQuery.propertyFilter.insert(query.parentProperty, Query::Comparator(QVariant()));
        }
    }

    auto transaction = getTransaction();

    Sink::EntityReader<DomainType> reader(*mDomainTypeAdaptorFactory, mResourceInstanceIdentifier, transaction);
    auto revisionAndReplayedEntities = reader.executeInitialQuery(modifiedQuery, offset, batchsize, resultProviderCallback(query, resultProvider));
    SinkTrace() << "Initial query took: " << Log::TraceTime(time.elapsed());
    return revisionAndReplayedEntities;
}

template class QueryRunner<Sink::ApplicationDomain::Folder>;
template class QueryRunner<Sink::ApplicationDomain::Mail>;
template class QueryRunner<Sink::ApplicationDomain::Event>;
template class QueryWorker<Sink::ApplicationDomain::Folder>;
template class QueryWorker<Sink::ApplicationDomain::Mail>;
template class QueryWorker<Sink::ApplicationDomain::Event>;
