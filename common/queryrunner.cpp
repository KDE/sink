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

#include <QTime>
#include "commands.h"
#include "log.h"
#include "storage.h"
#include "definitions.h"
#include "domainadaptor.h"
#include "asyncutils.h"

#undef DEBUG_AREA
#define DEBUG_AREA "client.queryrunner"

using namespace Sink;

/*
 * This class wraps the actual query implementation.
 *
 * This is a worker object that can be moved to a thread to execute the query.
 * The only interaction point is the ResultProvider, which handles the threadsafe reporting of the result.
 */
template<typename DomainType>
class QueryWorker : public QObject
{
public:
    QueryWorker(const Sink::Query &query, const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &, const QByteArray &bufferType, const QueryRunnerBase::ResultTransformation &transformation);
    virtual ~QueryWorker();

    qint64 executeIncrementalQuery(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);
    qint64 executeInitialQuery(const Sink::Query &query, const typename DomainType::Ptr &parent, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);

private:
    void replaySet(ResultSet &resultSet, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, const QList<QByteArray> &properties);

    void readEntity(const Sink::Storage::NamedDatabase &db, const QByteArray &key, const std::function<void(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &, Sink::Operation)> &resultCallback);

    ResultSet loadInitialResultSet(const Sink::Query &query, Sink::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters);
    ResultSet loadIncrementalResultSet(qint64 baseRevision, const Sink::Query &query, Sink::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters);

    ResultSet filterAndSortSet(ResultSet &resultSet, const std::function<bool(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter, const Sink::Storage::NamedDatabase &db, bool initialQuery, const QByteArray &sortProperty);
    std::function<bool(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> getFilter(const QSet<QByteArray> remainingFilters, const Sink::Query &query);
    qint64 load(const Sink::Query &query, const std::function<ResultSet(Sink::Storage::Transaction &, QSet<QByteArray> &)> &baseSetRetriever, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, bool initialQuery);

private:
    QueryRunnerBase::ResultTransformation mResultTransformation;
    DomainTypeAdaptorFactoryInterface::Ptr mDomainTypeAdaptorFactory;
    QByteArray mResourceInstanceIdentifier;
    QByteArray mBufferType;
    Sink::Query mQuery;
};


template<class DomainType>
QueryRunner<DomainType>::QueryRunner(const Sink::Query &query, const Sink::ResourceAccessInterface::Ptr &resourceAccess, const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &factory, const QByteArray &bufferType)
    : QueryRunnerBase(),
    mResourceAccess(resourceAccess),
    mResultProvider(new ResultProvider<typename DomainType::Ptr>)
{
    Trace() << "Starting query";
    //We delegate loading of initial data to the result provider, os it can decide for itself what it needs to load.
    mResultProvider->setFetcher([=](const typename DomainType::Ptr &parent) {
        Trace() << "Running fetcher";
        auto resultProvider = mResultProvider;
        async::run<qint64>([=]() -> qint64 {
                QueryWorker<DomainType> worker(query, instanceIdentifier, factory, bufferType, mResultTransformation);
                const qint64 newRevision = worker.executeInitialQuery(query, parent, *resultProvider);
                return newRevision;
            })
            .template then<void, qint64>([query, this](qint64 newRevision) {
                //Only send the revision replayed information if we're connected to the resource, there's no need to start the resource otherwise.
                if (query.liveQuery) {
                    mResourceAccess->sendRevisionReplayedCommand(newRevision);
                }
            }).exec();
    });

    // In case of a live query we keep the runner for as long alive as the result provider exists
    if (query.liveQuery) {
        //Incremental updates are always loaded directly, leaving it up to the result to discard the changes if they are not interesting
        setQuery([=] () -> KAsync::Job<void> {
            auto resultProvider = mResultProvider;
            return async::run<qint64>([=]() -> qint64 {
                    QueryWorker<DomainType> worker(query, instanceIdentifier, factory, bufferType, mResultTransformation);
                    const qint64 newRevision = worker.executeIncrementalQuery(query, *resultProvider);
                    return newRevision;
                })
                .template then<void, qint64>([query, this](qint64 newRevision) {
                    //Only send the revision replayed information if we're connected to the resource, there's no need to start the resource otherwise.
                    mResourceAccess->sendRevisionReplayedCommand(newRevision);
                });
        });
        //Ensure the connection is open, if it wasn't already opened
        //TODO If we are not connected already, we have to check for the latest revision once connected, otherwise we could miss some updates
        mResourceAccess->open();
        QObject::connect(mResourceAccess.data(), &Sink::ResourceAccess::revisionChanged, this, &QueryRunner::revisionChanged);
    }
}

template<class DomainType>
QueryRunner<DomainType>::~QueryRunner()
{
    Trace() << "Stopped query";
}

template<class DomainType>
void QueryRunner<DomainType>::setResultTransformation(const ResultTransformation &transformation)
{
    mResultTransformation = transformation;
}

template<class DomainType>
typename Sink::ResultEmitter<typename DomainType::Ptr>::Ptr QueryRunner<DomainType>::emitter()
{
    return mResultProvider->emitter();
}



static inline ResultSet fullScan(const Sink::Storage::Transaction &transaction, const QByteArray &bufferType)
{
    //TODO use a result set with an iterator, to read values on demand
    QVector<QByteArray> keys;
    Storage::mainDatabase(transaction, bufferType).scan(QByteArray(), [&](const QByteArray &key, const QByteArray &value) -> bool {
        //Skip internals
        if (Sink::Storage::isInternalKey(key)) {
            return true;
        }
        keys << Sink::Storage::uidFromKey(key);
        return true;
    },
    [](const Sink::Storage::Error &error) {
        Warning() << "Error during query: " << error.message;
    });

    Trace() << "Full scan on " << bufferType << " found " << keys.size() << " results";
    return ResultSet(keys);
}


template<class DomainType>
QueryWorker<DomainType>::QueryWorker(const Sink::Query &query, const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &factory, const QByteArray &bufferType, const QueryRunnerBase::ResultTransformation &transformation)
    : QObject(),
    mResultTransformation(transformation),
    mDomainTypeAdaptorFactory(factory),
    mResourceInstanceIdentifier(instanceIdentifier),
    mBufferType(bufferType),
    mQuery(query)
{
    Trace() << "Starting query worker";
}

template<class DomainType>
QueryWorker<DomainType>::~QueryWorker()
{
    Trace() << "Stopped query worker";
}

template<class DomainType>
void QueryWorker<DomainType>::replaySet(ResultSet &resultSet, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, const QList<QByteArray> &properties)
{
    int counter = 0;
    while (resultSet.next([this, &resultProvider, &counter, &properties](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &value, Sink::Operation operation) -> bool {
        //FIXME allow maildir resource to set the mimeMessage property
        auto valueCopy = Sink::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<DomainType>(*value, properties).template staticCast<DomainType>();
        if (mResultTransformation) {
            mResultTransformation(*valueCopy);
        }
        counter++;
        switch (operation) {
        case Sink::Operation_Creation:
            // Trace() << "Got creation";
            resultProvider.add(valueCopy);
            break;
        case Sink::Operation_Modification:
            // Trace() << "Got modification";
            resultProvider.modify(valueCopy);
            break;
        case Sink::Operation_Removal:
            // Trace() << "Got removal";
            resultProvider.remove(valueCopy);
            break;
        }
        return true;
    })){};
    Trace() << "Replayed " << counter << " results";
}

template<class DomainType>
void QueryWorker<DomainType>::readEntity(const Sink::Storage::NamedDatabase &db, const QByteArray &key, const std::function<void(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &, Sink::Operation)> &resultCallback)
{
    //This only works for a 1:1 mapping of resource to domain types.
    //Not i.e. for tags that are stored as flags in each entity of an imap store.
    //additional properties that don't have a 1:1 mapping (such as separately stored tags),
    //could be added to the adaptor.
    db.findLatest(key, [=](const QByteArray &key, const QByteArray &value) -> bool {
        Sink::EntityBuffer buffer(value.data(), value.size());
        const Sink::Entity &entity = buffer.entity();
        const auto metadataBuffer = Sink::EntityBuffer::readBuffer<Sink::Metadata>(entity.metadata());
        Q_ASSERT(metadataBuffer);
        const qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
        const auto operation = metadataBuffer ? metadataBuffer->operation() : Sink::Operation_Creation;
        resultCallback(DomainType::Ptr::create(mResourceInstanceIdentifier, Sink::Storage::uidFromKey(key), revision, mDomainTypeAdaptorFactory->createAdaptor(entity)), operation);
        return false;
    },
    [](const Sink::Storage::Error &error) {
        Warning() << "Error during query: " << error.message;
    });
}

template<class DomainType>
ResultSet QueryWorker<DomainType>::loadInitialResultSet(const Sink::Query &query, Sink::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters)
{
    if (!query.ids.isEmpty()) {
        return ResultSet(query.ids.toVector());
    }
    QSet<QByteArray> appliedFilters;
    auto resultSet = Sink::ApplicationDomain::TypeImplementation<DomainType>::queryIndexes(query, mResourceInstanceIdentifier, appliedFilters, transaction);
    remainingFilters = query.propertyFilter.keys().toSet() - appliedFilters;

    //We do a full scan if there were no indexes available to create the initial set.
    if (appliedFilters.isEmpty()) {
        //TODO this should be replaced by an index lookup as well
        resultSet = fullScan(transaction, mBufferType);
    }
    return resultSet;
}

template<class DomainType>
ResultSet QueryWorker<DomainType>::loadIncrementalResultSet(qint64 baseRevision, const Sink::Query &query, Sink::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters)
{
    const auto bufferType = mBufferType;
    auto revisionCounter = QSharedPointer<qint64>::create(baseRevision);
    remainingFilters = query.propertyFilter.keys().toSet();
    return ResultSet([bufferType, revisionCounter, &transaction]() -> QByteArray {
        const qint64 topRevision = Sink::Storage::maxRevision(transaction);
        //Spit out the revision keys one by one.
        while (*revisionCounter <= topRevision) {
            const auto uid = Sink::Storage::getUidFromRevision(transaction, *revisionCounter);
            const auto type = Sink::Storage::getTypeFromRevision(transaction, *revisionCounter);
            // Trace() << "Revision" << *revisionCounter << type << uid;
            if (type != bufferType) {
                //Skip revision
                *revisionCounter += 1;
                continue;
            }
            const auto key = Sink::Storage::assembleKey(uid, *revisionCounter);
            *revisionCounter += 1;
            return key;
        }
        Trace() << "Finished reading incremental result set:" << *revisionCounter;
        //We're done
        return QByteArray();
    });
}

template<class DomainType>
ResultSet QueryWorker<DomainType>::filterAndSortSet(ResultSet &resultSet, const std::function<bool(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter, const Sink::Storage::NamedDatabase &db, bool initialQuery, const QByteArray &sortProperty)
{
    auto sortedMap = QSharedPointer<QMap<QByteArray, QByteArray>>::create();

    if (initialQuery) {
        while (resultSet.next()) {
            //readEntity is only necessary if we actually want to filter or know the operation type (but not a big deal if we do it always I guess)
            readEntity(db, resultSet.id(), [this, filter, initialQuery, sortedMap, sortProperty, &resultSet](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject, Sink::Operation operation) {
                //We're not interested in removals during the initial query
                if ((operation != Sink::Operation_Removal) && filter(domainObject)) {
                    if (!sortProperty.isEmpty()) {
                        sortedMap->insert(domainObject->getProperty(sortProperty).toString().toLatin1(), domainObject->identifier());
                    } else {
                        sortedMap->insert(domainObject->identifier(), domainObject->identifier());
                    }
                }
            });
        }

        auto iterator = QSharedPointer<QMapIterator<QByteArray, QByteArray> >::create(*sortedMap);
        ResultSet::ValueGenerator generator = [this, iterator, sortedMap, &db, filter, initialQuery](std::function<void(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &, Sink::Operation)> callback) -> bool {
            if (iterator->hasNext()) {
                readEntity(db, iterator->next().value(), [this, filter, callback, initialQuery](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject, Sink::Operation operation) {
                    callback(domainObject, Sink::Operation_Creation);
                });
                return true;
            }
            return false;
        };
        return ResultSet(generator);
    } else {
        auto resultSetPtr = QSharedPointer<ResultSet>::create(resultSet);
        ResultSet::ValueGenerator generator = [this, resultSetPtr, &db, filter, initialQuery](std::function<void(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &, Sink::Operation)> callback) -> bool {
            if (resultSetPtr->next()) {
                //readEntity is only necessary if we actually want to filter or know the operation type (but not a big deal if we do it always I guess)
                readEntity(db, resultSetPtr->id(), [this, filter, callback, initialQuery](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject, Sink::Operation operation) {
                    //Always remove removals, they probably don't match due to non-available properties
                    if ((operation == Sink::Operation_Removal) || filter(domainObject)) {
                        callback(domainObject, operation);
                    }
                });
                return true;
            }
            return false;
        };
        return ResultSet(generator);
    }
}

template<class DomainType>
std::function<bool(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> QueryWorker<DomainType>::getFilter(const QSet<QByteArray> remainingFilters, const Sink::Query &query)
{
    return [remainingFilters, query](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject) -> bool {
        if (!query.ids.isEmpty()) {
            if (!query.ids.contains(domainObject->identifier())) {
                return false;
            }
        }
        for (const auto &filterProperty : remainingFilters) {
            const auto property = domainObject->getProperty(filterProperty);
            if (property.isValid()) {
                //TODO implement other comparison operators than equality
                if (property != query.propertyFilter.value(filterProperty)) {
                    Trace() << "Filtering entity due to property mismatch on filter: " << filterProperty << property << ":" << query.propertyFilter.value(filterProperty);
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
qint64 QueryWorker<DomainType>::load(const Sink::Query &query, const std::function<ResultSet(Sink::Storage::Transaction &, QSet<QByteArray> &)> &baseSetRetriever, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, bool initialQuery)
{
    QTime time;
    time.start();

    Sink::Storage storage(Sink::storageLocation(), mResourceInstanceIdentifier);
    storage.setDefaultErrorHandler([](const Sink::Storage::Error &error) {
        Warning() << "Error during query: " << error.store << error.message;
    });
    auto transaction = storage.createTransaction(Sink::Storage::ReadOnly);
    auto db = Storage::mainDatabase(transaction, mBufferType);

    QSet<QByteArray> remainingFilters;
    auto resultSet = baseSetRetriever(transaction, remainingFilters);
    Trace() << "Base set retrieved. " << time.elapsed();
    auto filteredSet = filterAndSortSet(resultSet, getFilter(remainingFilters, query), db, initialQuery, query.sortProperty);
    Trace() << "Filtered set retrieved. " << time.elapsed();
    replaySet(filteredSet, resultProvider, query.requestedProperties);
    Trace() << "Filtered set replayed. " << time.elapsed();
    resultProvider.setRevision(Sink::Storage::maxRevision(transaction));
    return Sink::Storage::maxRevision(transaction);
}

template<class DomainType>
qint64 QueryWorker<DomainType>::executeIncrementalQuery(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider)
{
    QTime time;
    time.start();

    const qint64 baseRevision = resultProvider.revision() + 1;
    Trace() << "Running incremental query " << baseRevision;
    auto revision = load(query, [&](Sink::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters) -> ResultSet {
        return loadIncrementalResultSet(baseRevision, query, transaction, remainingFilters);
    }, resultProvider, false);
    Trace() << "Incremental query took: " << time.elapsed() << " ms";
    return revision;
}

template<class DomainType>
qint64 QueryWorker<DomainType>::executeInitialQuery(const Sink::Query &query, const typename DomainType::Ptr &parent, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider)
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
    auto revision = load(modifiedQuery, [&](Sink::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters) -> ResultSet {
        return loadInitialResultSet(modifiedQuery, transaction, remainingFilters);
    }, resultProvider, true);
    Trace() << "Initial query took: " << time.elapsed() << " ms";
    resultProvider.initialResultSetComplete(parent);
    return revision;
}

template class QueryRunner<Sink::ApplicationDomain::Folder>;
template class QueryRunner<Sink::ApplicationDomain::Mail>;
template class QueryRunner<Sink::ApplicationDomain::Event>;
template class QueryWorker<Sink::ApplicationDomain::Folder>;
template class QueryWorker<Sink::ApplicationDomain::Mail>;
template class QueryWorker<Sink::ApplicationDomain::Event>;
