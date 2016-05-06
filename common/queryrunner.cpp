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
template <typename DomainType>
class QueryWorker : public QObject
{
public:
    QueryWorker(const Sink::Query &query, const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &, const QByteArray &bufferType,
        const QueryRunnerBase::ResultTransformation &transformation);
    virtual ~QueryWorker();

    QPair<qint64, qint64> executeIncrementalQuery(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);
    QPair<qint64, qint64> executeInitialQuery(const Sink::Query &query, const typename DomainType::Ptr &parent, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, int offset, int batchsize);

private:
    qint64 replaySet(ResultSet &resultSet, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, const QList<QByteArray> &properties, int offset, int batchSize);

    void readEntity(const Sink::Storage::NamedDatabase &db, const QByteArray &key,
        const std::function<void(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &, Sink::Operation)> &resultCallback);

    ResultSet loadInitialResultSet(const Sink::Query &query, Sink::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters, QByteArray &remainingSorting);
    ResultSet loadIncrementalResultSet(qint64 baseRevision, const Sink::Query &query, Sink::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters);

    ResultSet filterAndSortSet(ResultSet &resultSet, const std::function<bool(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter,
        const Sink::Storage::NamedDatabase &db, bool initialQuery, const QByteArray &sortProperty);
    std::function<bool(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> getFilter(const QSet<QByteArray> remainingFilters, const Sink::Query &query);
    QPair<qint64, qint64> load(const Sink::Query &query, const std::function<ResultSet(Sink::Storage::Transaction &, QSet<QByteArray> &, QByteArray &)> &baseSetRetriever,
        Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, bool initialQuery, int offset, int batchSize);

private:
    QueryRunnerBase::ResultTransformation mResultTransformation;
    DomainTypeAdaptorFactoryInterface::Ptr mDomainTypeAdaptorFactory;
    QByteArray mResourceInstanceIdentifier;
    QByteArray mBufferType;
    Sink::Query mQuery;
};


template <class DomainType>
QueryRunner<DomainType>::QueryRunner(const Sink::Query &query, const Sink::ResourceAccessInterface::Ptr &resourceAccess, const QByteArray &instanceIdentifier,
    const DomainTypeAdaptorFactoryInterface::Ptr &factory, const QByteArray &bufferType)
    : QueryRunnerBase(), mResourceAccess(resourceAccess), mResultProvider(new ResultProvider<typename DomainType::Ptr>), mBatchSize(query.limit)
{
    Trace() << "Starting query";
    if (query.limit && query.sortProperty.isEmpty()) {
        Warning() << "A limited query without sorting is typically a bad idea.";
    }
    // We delegate loading of initial data to the result provider, so it can decide for itself what it needs to load.
    mResultProvider->setFetcher([=](const typename DomainType::Ptr &parent) {
        const QByteArray parentId = parent ? parent->identifier() : QByteArray();
        Trace() << "Running fetcher. Offset: " << mOffset[parentId] << " Batchsize: " << mBatchSize;
        auto resultProvider = mResultProvider;
        async::run<QPair<qint64, qint64> >([=]() {
            QueryWorker<DomainType> worker(query, instanceIdentifier, factory, bufferType, mResultTransformation);
            const auto  newRevisionAndReplayedEntities = worker.executeInitialQuery(query, parent, *resultProvider, mOffset[parentId], mBatchSize);
            return newRevisionAndReplayedEntities;
        })
            .template then<void, QPair<qint64, qint64>>([=](const QPair<qint64, qint64> &newRevisionAndReplayedEntities) {
                mOffset[parentId] += newRevisionAndReplayedEntities.second;
                // Only send the revision replayed information if we're connected to the resource, there's no need to start the resource otherwise.
                if (query.liveQuery) {
                    mResourceAccess->sendRevisionReplayedCommand(newRevisionAndReplayedEntities.first);
                }
            })
            .exec();
    });

    // In case of a live query we keep the runner for as long alive as the result provider exists
    if (query.liveQuery) {
        // Incremental updates are always loaded directly, leaving it up to the result to discard the changes if they are not interesting
        setQuery([=]() -> KAsync::Job<void> {
            auto resultProvider = mResultProvider;
            return async::run<QPair<qint64, qint64> >([=]() {
                       QueryWorker<DomainType> worker(query, instanceIdentifier, factory, bufferType, mResultTransformation);
                       const auto newRevisionAndReplayedEntities = worker.executeIncrementalQuery(query, *resultProvider);
                       return newRevisionAndReplayedEntities;
                   })
                .template then<void, QPair<qint64, qint64> >([query, this](const QPair<qint64, qint64> &newRevisionAndReplayedEntities) {
                    // Only send the revision replayed information if we're connected to the resource, there's no need to start the resource otherwise.
                    mResourceAccess->sendRevisionReplayedCommand(newRevisionAndReplayedEntities.first);
                });
        });
        // Ensure the connection is open, if it wasn't already opened
        // TODO If we are not connected already, we have to check for the latest revision once connected, otherwise we could miss some updates
        mResourceAccess->open();
        QObject::connect(mResourceAccess.data(), &Sink::ResourceAccess::revisionChanged, this, &QueryRunner::revisionChanged);
    }
}

template <class DomainType>
QueryRunner<DomainType>::~QueryRunner()
{
    Trace() << "Stopped query";
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


static inline ResultSet fullScan(const Sink::Storage::Transaction &transaction, const QByteArray &bufferType)
{
    // TODO use a result set with an iterator, to read values on demand
    QVector<QByteArray> keys;
    Storage::mainDatabase(transaction, bufferType)
        .scan(QByteArray(),
            [&](const QByteArray &key, const QByteArray &value) -> bool {
                // Skip internals
                if (Sink::Storage::isInternalKey(key)) {
                    return true;
                }
                keys << Sink::Storage::uidFromKey(key);
                return true;
            },
            [](const Sink::Storage::Error &error) { Warning() << "Error during query: " << error.message; });

    Trace() << "Full scan retrieved " << keys.size() << " results.";
    return ResultSet(keys);
}


template <class DomainType>
QueryWorker<DomainType>::QueryWorker(const Sink::Query &query, const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &factory,
    const QByteArray &bufferType, const QueryRunnerBase::ResultTransformation &transformation)
    : QObject(), mResultTransformation(transformation), mDomainTypeAdaptorFactory(factory), mResourceInstanceIdentifier(instanceIdentifier), mBufferType(bufferType), mQuery(query)
{
    Trace() << "Starting query worker";
}

template <class DomainType>
QueryWorker<DomainType>::~QueryWorker()
{
    Trace() << "Stopped query worker";
}

template <class DomainType>
qint64 QueryWorker<DomainType>::replaySet(ResultSet &resultSet, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, const QList<QByteArray> &properties, int offset, int batchSize)
{
    Trace() << "Skipping over " << offset << " results";
    resultSet.skip(offset);
    int counter = 0;
    while (!batchSize || (counter < batchSize)) {
        const bool ret =
            resultSet.next([this, &resultProvider, &counter, &properties, batchSize](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &value, Sink::Operation operation) -> bool {
                counter++;
                auto valueCopy = Sink::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<DomainType>(*value, properties).template staticCast<DomainType>();
                if (mResultTransformation) {
                    mResultTransformation(*valueCopy);
                }
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
            });
        if (!ret) {
            break;
        }
    };
    Trace() << "Replayed " << counter << " results."
            << "Limit " << batchSize;
    return counter;
}

template <class DomainType>
void QueryWorker<DomainType>::readEntity(const Sink::Storage::NamedDatabase &db, const QByteArray &key,
    const std::function<void(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &, Sink::Operation)> &resultCallback)
{
    // This only works for a 1:1 mapping of resource to domain types.
    // Not i.e. for tags that are stored as flags in each entity of an imap store.
    // additional properties that don't have a 1:1 mapping (such as separately stored tags),
    // could be added to the adaptor.
    db.findLatest(key,
        [=](const QByteArray &key, const QByteArray &value) -> bool {
            Sink::EntityBuffer buffer(value.data(), value.size());
            const Sink::Entity &entity = buffer.entity();
            const auto metadataBuffer = Sink::EntityBuffer::readBuffer<Sink::Metadata>(entity.metadata());
            const qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
            const auto operation = metadataBuffer ? metadataBuffer->operation() : Sink::Operation_Creation;
            auto adaptor = mDomainTypeAdaptorFactory->createAdaptor(entity);
            resultCallback(DomainType::Ptr::create(mResourceInstanceIdentifier, Sink::Storage::uidFromKey(key), revision, adaptor), operation);
            return false;
        },
        [](const Sink::Storage::Error &error) { Warning() << "Error during query: " << error.message; });
}

template <class DomainType>
ResultSet QueryWorker<DomainType>::loadInitialResultSet(const Sink::Query &query, Sink::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters, QByteArray &remainingSorting)
{
    if (!query.ids.isEmpty()) {
        return ResultSet(query.ids.toVector());
    }
    QSet<QByteArray> appliedFilters;
    QByteArray appliedSorting;
    auto resultSet = Sink::ApplicationDomain::TypeImplementation<DomainType>::queryIndexes(query, mResourceInstanceIdentifier, appliedFilters, appliedSorting, transaction);
    remainingFilters = query.propertyFilter.keys().toSet() - appliedFilters;
    if (appliedSorting.isEmpty()) {
        remainingSorting = query.sortProperty;
    }

    // We do a full scan if there were no indexes available to create the initial set.
    if (appliedFilters.isEmpty()) {
        // TODO this should be replaced by an index lookup as well
        resultSet = fullScan(transaction, mBufferType);
    }
    return resultSet;
}

template <class DomainType>
ResultSet QueryWorker<DomainType>::loadIncrementalResultSet(qint64 baseRevision, const Sink::Query &query, Sink::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters)
{
    const auto bufferType = mBufferType;
    auto revisionCounter = QSharedPointer<qint64>::create(baseRevision);
    remainingFilters = query.propertyFilter.keys().toSet();
    return ResultSet([bufferType, revisionCounter, &transaction]() -> QByteArray {
        const qint64 topRevision = Sink::Storage::maxRevision(transaction);
        // Spit out the revision keys one by one.
        while (*revisionCounter <= topRevision) {
            const auto uid = Sink::Storage::getUidFromRevision(transaction, *revisionCounter);
            const auto type = Sink::Storage::getTypeFromRevision(transaction, *revisionCounter);
            // Trace() << "Revision" << *revisionCounter << type << uid;
            Q_ASSERT(!uid.isEmpty());
            Q_ASSERT(!type.isEmpty());
            if (type != bufferType) {
                // Skip revision
                *revisionCounter += 1;
                continue;
            }
            const auto key = Sink::Storage::assembleKey(uid, *revisionCounter);
            *revisionCounter += 1;
            return key;
        }
        Trace() << "Finished reading incremental result set:" << *revisionCounter;
        // We're done
        return QByteArray();
    });
}

template <class DomainType>
ResultSet QueryWorker<DomainType>::filterAndSortSet(ResultSet &resultSet, const std::function<bool(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter,
    const Sink::Storage::NamedDatabase &db, bool initialQuery, const QByteArray &sortProperty)
{
    const bool sortingRequired = !sortProperty.isEmpty();
    if (initialQuery && sortingRequired) {
        Trace() << "Sorting the resultset in memory according to property: " << sortProperty;
        // Sort the complete set by reading the sort property and filling into a sorted map
        auto sortedMap = QSharedPointer<QMap<QByteArray, QByteArray>>::create();
        while (resultSet.next()) {
            // readEntity is only necessary if we actually want to filter or know the operation type (but not a big deal if we do it always I guess)
            readEntity(db, resultSet.id(),
                [this, filter, initialQuery, sortedMap, sortProperty, &resultSet](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject, Sink::Operation operation) {
                    // We're not interested in removals during the initial query
                    if ((operation != Sink::Operation_Removal) && filter(domainObject)) {
                        if (!sortProperty.isEmpty()) {
                            const auto sortValue = domainObject->getProperty(sortProperty);
                            if (sortValue.type() == QVariant::DateTime) {
                                sortedMap->insert(QByteArray::number(std::numeric_limits<unsigned int>::max() - sortValue.toDateTime().toTime_t()), domainObject->identifier());
                            } else {
                                sortedMap->insert(sortValue.toString().toLatin1(), domainObject->identifier());
                            }
                        } else {
                            sortedMap->insert(domainObject->identifier(), domainObject->identifier());
                        }
                    }
                });
        }

        Trace() << "Sorted " << sortedMap->size() << " values.";
        auto iterator = QSharedPointer<QMapIterator<QByteArray, QByteArray>>::create(*sortedMap);
        ResultSet::ValueGenerator generator = [this, iterator, sortedMap, &db, filter, initialQuery](
            std::function<void(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &, Sink::Operation)> callback) -> bool {
            if (iterator->hasNext()) {
                readEntity(db, iterator->next().value(), [this, filter, callback, initialQuery](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject,
                                                             Sink::Operation operation) { callback(domainObject, Sink::Operation_Creation); });
                return true;
            }
            return false;
        };

        auto skip = [iterator]() {
            if (iterator->hasNext()) {
                iterator->next();
            }
        };
        return ResultSet(generator, skip);
    } else {
        auto resultSetPtr = QSharedPointer<ResultSet>::create(resultSet);
        ResultSet::ValueGenerator generator = [this, resultSetPtr, &db, filter, initialQuery](
            std::function<void(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &, Sink::Operation)> callback) -> bool {
            if (resultSetPtr->next()) {
                // readEntity is only necessary if we actually want to filter or know the operation type (but not a big deal if we do it always I guess)
                readEntity(db, resultSetPtr->id(), [this, filter, callback, initialQuery](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject, Sink::Operation operation) {
                    if (initialQuery) {
                        // We're not interested in removals during the initial query
                        if ((operation != Sink::Operation_Removal) && filter(domainObject)) {
                            // In the initial set every entity is new
                            callback(domainObject, Sink::Operation_Creation);
                        }
                    } else {
                        // Always remove removals, they probably don't match due to non-available properties
                        if ((operation == Sink::Operation_Removal) || filter(domainObject)) {
                            // TODO only replay if this is in the currently visible set (or just always replay, worst case we have a couple to many results)
                            callback(domainObject, operation);
                        }
                    }
                });
                return true;
            }
            return false;
        };
        auto skip = [resultSetPtr]() { resultSetPtr->skip(1); };
        return ResultSet(generator, skip);
    }
}

template <class DomainType>
std::function<bool(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)>
QueryWorker<DomainType>::getFilter(const QSet<QByteArray> remainingFilters, const Sink::Query &query)
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
                const auto comparator = query.propertyFilter.value(filterProperty);
                if (!comparator.matches(property)) {
                    Trace() << "Filtering entity due to property mismatch on filter: " << filterProperty << property << ":" << comparator.value;
                    return false;
                }
            } else {
                Warning() << "Ignored property filter because value is invalid: " << filterProperty;
            }
        }
        return true;
    };
}

template <class DomainType>
QPair<qint64, qint64> QueryWorker<DomainType>::load(const Sink::Query &query, const std::function<ResultSet(Sink::Storage::Transaction &, QSet<QByteArray> &, QByteArray &)> &baseSetRetriever,
    Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, bool initialQuery, int offset, int batchSize)
{
    QTime time;
    time.start();

    Sink::Storage storage(Sink::storageLocation(), mResourceInstanceIdentifier);
    storage.setDefaultErrorHandler([](const Sink::Storage::Error &error) { Warning() << "Error during query: " << error.store << error.message; });
    auto transaction = storage.createTransaction(Sink::Storage::ReadOnly);
    auto db = Storage::mainDatabase(transaction, mBufferType);

    QSet<QByteArray> remainingFilters;
    QByteArray remainingSorting;
    auto resultSet = baseSetRetriever(transaction, remainingFilters, remainingSorting);
    Trace() << "Base set retrieved. " << Log::TraceTime(time.elapsed());
    auto filteredSet = filterAndSortSet(resultSet, getFilter(remainingFilters, query), db, initialQuery, remainingSorting);
    Trace() << "Filtered set retrieved. " << Log::TraceTime(time.elapsed());
    auto replayedEntities = replaySet(filteredSet, resultProvider, query.requestedProperties, offset, batchSize);
    Trace() << "Filtered set replayed. " << Log::TraceTime(time.elapsed());
    resultProvider.setRevision(Sink::Storage::maxRevision(transaction));
    return qMakePair(Sink::Storage::maxRevision(transaction), replayedEntities);
}

template <class DomainType>
QPair<qint64, qint64> QueryWorker<DomainType>::executeIncrementalQuery(const Sink::Query &query, Sink::ResultProviderInterface<typename DomainType::Ptr> &resultProvider)
{
    QTime time;
    time.start();

    const qint64 baseRevision = resultProvider.revision() + 1;
    Trace() << "Running incremental query " << baseRevision;
    auto revisionAndReplayedEntities = load(query, [&](Sink::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters, QByteArray &remainingSorting) -> ResultSet {
        return loadIncrementalResultSet(baseRevision, query, transaction, remainingFilters);
    }, resultProvider, false, 0, 0);
    Trace() << "Incremental query took: " << Log::TraceTime(time.elapsed());
    return revisionAndReplayedEntities;
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
            Trace() << "Running initial query for parent:" << parent->identifier();
            modifiedQuery.propertyFilter.insert(query.parentProperty, Query::Comparator(parent->identifier()));
        } else {
            Trace() << "Running initial query for toplevel";
            modifiedQuery.propertyFilter.insert(query.parentProperty, Query::Comparator(QVariant()));
        }
    }
    auto revisionAndReplayedEntities = load(modifiedQuery, [&](Sink::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters, QByteArray &remainingSorting) -> ResultSet {
        return loadInitialResultSet(modifiedQuery, transaction, remainingFilters, remainingSorting);
    }, resultProvider, true, offset, batchsize);
    Trace() << "Initial query took: " << Log::TraceTime(time.elapsed());
    resultProvider.initialResultSetComplete(parent);
    return revisionAndReplayedEntities;
}

template class QueryRunner<Sink::ApplicationDomain::Folder>;
template class QueryRunner<Sink::ApplicationDomain::Mail>;
template class QueryRunner<Sink::ApplicationDomain::Event>;
template class QueryWorker<Sink::ApplicationDomain::Folder>;
template class QueryWorker<Sink::ApplicationDomain::Mail>;
template class QueryWorker<Sink::ApplicationDomain::Event>;
