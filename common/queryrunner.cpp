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

#include <QtConcurrent/QtConcurrentRun>
#include <QFuture>
#include <QFutureWatcher>
#include <QTime>
#include "commands.h"
#include "log.h"
#include "storage.h"
#include "definitions.h"
#include "domainadaptor.h"

using namespace Akonadi2;

static inline ResultSet fullScan(const Akonadi2::Storage::Transaction &transaction, const QByteArray &bufferType)
{
    //TODO use a result set with an iterator, to read values on demand
    QVector<QByteArray> keys;
    transaction.openDatabase(bufferType + ".main").scan(QByteArray(), [&](const QByteArray &key, const QByteArray &value) -> bool {
        //Skip internals
        if (Akonadi2::Storage::isInternalKey(key)) {
            return true;
        }
        keys << Akonadi2::Storage::uidFromKey(key);
        return true;
    },
    [](const Akonadi2::Storage::Error &error) {
        qWarning() << "Error during query: " << error.message;
    });

    Trace() << "Full scan on " << bufferType << " found " << keys.size() << " results";
    return ResultSet(keys);
}

template<class DomainType>
QueryRunner<DomainType>::QueryRunner(const Akonadi2::Query &query, const Akonadi2::ResourceAccessInterface::Ptr &resourceAccess, const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &factory, const QByteArray &bufferType)
    : QueryRunnerBase(),
    mResourceAccess(resourceAccess),
    mResultProvider(new ResultProvider<typename DomainType::Ptr>),
    mDomainTypeAdaptorFactory(factory),
    mQuery(query),
    mResourceInstanceIdentifier(instanceIdentifier),
    mBufferType(bufferType)
{
    Trace() << "Starting query";
    //We delegate loading of initial data to the result provider, os it can decide for itself what it needs to load.
    mResultProvider->setFetcher([this, query](const typename DomainType::Ptr &parent) {
        Trace() << "Running fetcher";

        // auto watcher = new QFutureWatcher<qint64>;
        // QObject::connect(watcher, &QFutureWatcher::finished, [](qint64 newRevision) {
        //     mResourceAccess->sendRevisionReplayedCommand(newRevision);
        // });
        // auto future = QtConcurrent::run([&resultProvider]() -> qint64 {
        //     const qint64 newRevision = executeInitialQuery(query, parent, resultProvider);
        //     return newRevision;
        // });
        // watcher->setFuture(future);
        const qint64 newRevision = executeInitialQuery(query, parent, *mResultProvider);
        mResourceAccess->sendRevisionReplayedCommand(newRevision);
    });


    //In case of a live query we keep the runner for as long alive as the result provider exists
    if (query.liveQuery) {
        //Incremental updates are always loaded directly, leaving it up to the result to discard the changes if they are not interesting
        setQuery([this, query] () -> KAsync::Job<void> {
            return KAsync::start<void>([this, query](KAsync::Future<void> &future) {
                //TODO execute in thread
                const qint64 newRevision = executeIncrementalQuery(query, *mResultProvider);
                mResourceAccess->sendRevisionReplayedCommand(newRevision);
                future.setFinished();
            });
        });
        //Ensure the connection is open, if it wasn't already opened
        //TODO If we are not connected already, we have to check for the latest revision once connected, otherwise we could miss some updates
        mResourceAccess->open();
        QObject::connect(mResourceAccess.data(), &Akonadi2::ResourceAccess::revisionChanged, this, &QueryRunner::revisionChanged);
    }
}

template<class DomainType>
QueryRunner<DomainType>::~QueryRunner()
{
    Trace() << "Stopped query";
}

template<class DomainType>
typename Akonadi2::ResultEmitter<typename DomainType::Ptr>::Ptr QueryRunner<DomainType>::emitter()
{
    return mResultProvider->emitter();
}

//TODO move into result provider?
template<class DomainType>
void QueryRunner<DomainType>::replaySet(ResultSet &resultSet, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider)
{
    // Trace() << "Replay set";
    int counter = 0;
    while (resultSet.next([&resultProvider, &counter](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &value, Akonadi2::Operation operation) -> bool {
        counter++;
        switch (operation) {
        case Akonadi2::Operation_Creation:
            // Trace() << "Got creation";
            resultProvider.add(Akonadi2::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<DomainType>(*value).template staticCast<DomainType>());
            break;
        case Akonadi2::Operation_Modification:
            // Trace() << "Got modification";
            resultProvider.modify(Akonadi2::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<DomainType>(*value).template staticCast<DomainType>());
            break;
        case Akonadi2::Operation_Removal:
            // Trace() << "Got removal";
            resultProvider.remove(Akonadi2::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<DomainType>(*value).template staticCast<DomainType>());
            break;
        }
        return true;
    })){};
    Trace() << "Replayed " << counter << " results";
}

template<class DomainType>
void QueryRunner<DomainType>::readEntity(const Akonadi2::Storage::NamedDatabase &db, const QByteArray &key, const std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &, Akonadi2::Operation)> &resultCallback)
{
    //This only works for a 1:1 mapping of resource to domain types.
    //Not i.e. for tags that are stored as flags in each entity of an imap store.
    //additional properties that don't have a 1:1 mapping (such as separately stored tags),
    //could be added to the adaptor.
    db.findLatest(key, [=](const QByteArray &key, const QByteArray &value) -> bool {
        Akonadi2::EntityBuffer buffer(value.data(), value.size());
        const Akonadi2::Entity &entity = buffer.entity();
        const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(entity.metadata());
        Q_ASSERT(metadataBuffer);
        const qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
        resultCallback(DomainType::Ptr::create(mResourceInstanceIdentifier, Akonadi2::Storage::uidFromKey(key), revision, mDomainTypeAdaptorFactory->createAdaptor(entity)), metadataBuffer->operation());
        return false;
    },
    [](const Akonadi2::Storage::Error &error) {
        qWarning() << "Error during query: " << error.message;
    });
}

template<class DomainType>
ResultSet QueryRunner<DomainType>::loadInitialResultSet(const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters)
{
    QSet<QByteArray> appliedFilters;
    auto resultSet = Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::queryIndexes(query, mResourceInstanceIdentifier, appliedFilters, transaction);
    remainingFilters = query.propertyFilter.keys().toSet() - appliedFilters;

    //We do a full scan if there were no indexes available to create the initial set.
    if (appliedFilters.isEmpty()) {
        //TODO this should be replaced by an index lookup as well
        resultSet = fullScan(transaction, mBufferType);
    }
    return resultSet;
}

template<class DomainType>
ResultSet QueryRunner<DomainType>::loadIncrementalResultSet(qint64 baseRevision, const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters)
{
    const auto bufferType = mBufferType;
    auto revisionCounter = QSharedPointer<qint64>::create(baseRevision);
    remainingFilters = query.propertyFilter.keys().toSet();
    return ResultSet([bufferType, revisionCounter, &transaction]() -> QByteArray {
        const qint64 topRevision = Akonadi2::Storage::maxRevision(transaction);
        //Spit out the revision keys one by one.
        while (*revisionCounter <= topRevision) {
            const auto uid = Akonadi2::Storage::getUidFromRevision(transaction, *revisionCounter);
            const auto type = Akonadi2::Storage::getTypeFromRevision(transaction, *revisionCounter);
            // Trace() << "Revision" << *revisionCounter << type << uid;
            if (type != bufferType) {
                //Skip revision
                *revisionCounter += 1;
                continue;
            }
            const auto key = Akonadi2::Storage::assembleKey(uid, *revisionCounter);
            *revisionCounter += 1;
            return key;
        }
        Trace() << "Finished reading incremental result set:" << *revisionCounter;
        //We're done
        return QByteArray();
    });
}

template<class DomainType>
ResultSet QueryRunner<DomainType>::filterSet(const ResultSet &resultSet, const std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter, const Akonadi2::Storage::NamedDatabase &db, bool initialQuery)
{
    auto resultSetPtr = QSharedPointer<ResultSet>::create(resultSet);

    //Read through the source values and return whatever matches the filter
    std::function<bool(std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &, Akonadi2::Operation)>)> generator = [this, resultSetPtr, &db, filter, initialQuery](std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &, Akonadi2::Operation)> callback) -> bool {
        while (resultSetPtr->next()) {
            //readEntity is only necessary if we actually want to filter or know the operation type (but not a big deal if we do it always I guess)
            readEntity(db, resultSetPtr->id(), [this, filter, callback, initialQuery](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject, Akonadi2::Operation operation) {
                //Always remove removals, they probably don't match due to non-available properties
                if (filter(domainObject) || operation == Akonadi2::Operation_Removal) {
                    if (initialQuery) {
                        //We're not interested in removals during the initial query
                        if (operation != Akonadi2::Operation_Removal) {
                            callback(domainObject, Akonadi2::Operation_Creation);
                        }
                    } else {
                        callback(domainObject, operation);
                    }
                }
            });
        }
        return false;
    };
    return ResultSet(generator);
}


template<class DomainType>
std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> QueryRunner<DomainType>::getFilter(const QSet<QByteArray> remainingFilters, const Akonadi2::Query &query)
{
    return [remainingFilters, query](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject) -> bool {
        for (const auto &filterProperty : remainingFilters) {
            const auto property = domainObject->getProperty(filterProperty);
            if (property.isValid()) {
                //TODO implement other comparison operators than equality
                if (property != query.propertyFilter.value(filterProperty)) {
                    Trace() << "Filtering entity due to property mismatch: " << domainObject->getProperty(filterProperty);
                    return false;
                }
            } else {
                Warning() << "Ignored property filter because value is invalid: " << filterProperty;
            }
        }
        return true;
    };
}

template<class DomainType>
qint64 QueryRunner<DomainType>::load(const Akonadi2::Query &query, const std::function<ResultSet(Akonadi2::Storage::Transaction &, QSet<QByteArray> &)> &baseSetRetriever, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, bool initialQuery)
{
    Akonadi2::Storage storage(Akonadi2::storageLocation(), mResourceInstanceIdentifier);
    storage.setDefaultErrorHandler([](const Akonadi2::Storage::Error &error) {
        Warning() << "Error during query: " << error.store << error.message;
    });
    auto transaction = storage.createTransaction(Akonadi2::Storage::ReadOnly);
    auto db = transaction.openDatabase(mBufferType + ".main");

    QSet<QByteArray> remainingFilters;
    auto resultSet = baseSetRetriever(transaction, remainingFilters);
    auto filteredSet = filterSet(resultSet, getFilter(remainingFilters, query), db, initialQuery);
    replaySet(filteredSet, resultProvider);
    resultProvider.setRevision(Akonadi2::Storage::maxRevision(transaction));
    return Akonadi2::Storage::maxRevision(transaction);
}


template<class DomainType>
qint64 QueryRunner<DomainType>::executeIncrementalQuery(const Akonadi2::Query &query, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider)
{
    QTime time;
    time.start();

    const qint64 baseRevision = resultProvider.revision() + 1;
    Trace() << "Running incremental query " << baseRevision;
    auto revision = load(query, [&](Akonadi2::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters) -> ResultSet {
        return loadIncrementalResultSet(baseRevision, query, transaction, remainingFilters);
    }, resultProvider, false);
    Trace() << "Incremental query took: " << time.elapsed() << " ms";
    return revision;
}

template<class DomainType>
qint64 QueryRunner<DomainType>::executeInitialQuery(const Akonadi2::Query &query, const typename DomainType::Ptr &parent, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider)
{
    QTime time;
    time.start();

    auto modifiedQuery = query;
    if (!query.parentProperty.isEmpty()) {
        if (parent) {
            Trace() << "Running initial query for parent:" << parent->identifier();
            modifiedQuery.propertyFilter.insert(query.parentProperty, parent->identifier());
        } else {
            Trace() << "Running initial query for toplevel";
            modifiedQuery.propertyFilter.insert(query.parentProperty, QVariant());
        }
    }
    auto revision = load(modifiedQuery, [&](Akonadi2::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters) -> ResultSet {
        return loadInitialResultSet(modifiedQuery, transaction, remainingFilters);
    }, resultProvider, true);
    Trace() << "Initial query took: " << time.elapsed() << " ms";
    resultProvider.initialResultSetComplete(parent);
    return revision;
}

template class QueryRunner<Akonadi2::ApplicationDomain::Folder>;
template class QueryRunner<Akonadi2::ApplicationDomain::Mail>;
template class QueryRunner<Akonadi2::ApplicationDomain::Event>;
