/*
 * Copyright (C) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "entityreader.h"

#include "resultset.h"
#include "storage.h"
#include "query.h"

SINK_DEBUG_AREA("entityreader")

using namespace Sink;

QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> EntityReaderUtils::getLatest(const Sink::Storage::NamedDatabase &db, const QByteArray &uid, DomainTypeAdaptorFactoryInterface &adaptorFactory, qint64 &retrievedRevision)
{
    QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> current;
    db.findLatest(uid,
        [&current, &adaptorFactory, &retrievedRevision](const QByteArray &key, const QByteArray &data) -> bool {
            Sink::EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
            if (!buffer.isValid()) {
                SinkWarning() << "Read invalid buffer from disk";
            } else {
                SinkTrace() << "Found value " << key;
                current = adaptorFactory.createAdaptor(buffer.entity());
                retrievedRevision = Sink::Storage::revisionFromKey(key);
            }
            return false;
        },
        [](const Sink::Storage::Error &error) { SinkWarning() << "Failed to read current value from storage: " << error.message; });
    return current;
}

QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> EntityReaderUtils::get(const Sink::Storage::NamedDatabase &db, const QByteArray &key, DomainTypeAdaptorFactoryInterface &adaptorFactory, qint64 &retrievedRevision)
{
    QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> current;
    db.scan(key,
        [&current, &adaptorFactory, &retrievedRevision](const QByteArray &key, const QByteArray &data) -> bool {
            Sink::EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
            if (!buffer.isValid()) {
                SinkWarning() << "Read invalid buffer from disk";
            } else {
                current = adaptorFactory.createAdaptor(buffer.entity());
                retrievedRevision = Sink::Storage::revisionFromKey(key);
            }
            return false;
        },
        [](const Sink::Storage::Error &error) { SinkWarning() << "Failed to read current value from storage: " << error.message; });
    return current;
}

QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> EntityReaderUtils::getPrevious(const Sink::Storage::NamedDatabase &db, const QByteArray &uid, qint64 revision, DomainTypeAdaptorFactoryInterface &adaptorFactory, qint64 &retrievedRevision)
{
    QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> current;
    qint64 latestRevision = 0;
    db.scan(uid,
        [&current, &latestRevision, revision](const QByteArray &key, const QByteArray &) -> bool {
            auto foundRevision = Sink::Storage::revisionFromKey(key);
            if (foundRevision < revision && foundRevision > latestRevision) {
                latestRevision = foundRevision;
            }
            return true;
        },
        [](const Sink::Storage::Error &error) { SinkWarning() << "Failed to read current value from storage: " << error.message; }, true);
    return get(db, Sink::Storage::assembleKey(uid, latestRevision), adaptorFactory, retrievedRevision);
}

template <class DomainType>
EntityReader<DomainType>::EntityReader(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier, Sink::Storage::Transaction &transaction)
    : mResourceInstanceIdentifier(resourceInstanceIdentifier),
    mTransaction(transaction),
    mDomainTypeAdaptorFactoryPtr(Sink::AdaptorFactoryRegistry::instance().getFactory<DomainType>(resourceType)),
    mDomainTypeAdaptorFactory(*mDomainTypeAdaptorFactoryPtr)
{
    Q_ASSERT(!resourceType.isEmpty());
    SinkTrace() << "resourceType " << resourceType;
    Q_ASSERT(mDomainTypeAdaptorFactoryPtr);
}

template <class DomainType>
EntityReader<DomainType>::EntityReader(DomainTypeAdaptorFactoryInterface &domainTypeAdaptorFactory, const QByteArray &resourceInstanceIdentifier, Sink::Storage::Transaction &transaction)
    : mResourceInstanceIdentifier(resourceInstanceIdentifier),
    mTransaction(transaction),
    mDomainTypeAdaptorFactory(domainTypeAdaptorFactory)
{

}

template <class DomainType>
DomainType EntityReader<DomainType>::read(const QByteArray &identifier) const
{
    auto typeName = ApplicationDomain::getTypeName<DomainType>();
    auto mainDatabase = Storage::mainDatabase(mTransaction, typeName);
    qint64 retrievedRevision = 0;
    auto bufferAdaptor = EntityReaderUtils::getLatest(mainDatabase, identifier, mDomainTypeAdaptorFactory, retrievedRevision);
    if (!bufferAdaptor) {
        return DomainType();
    }
    return DomainType(mResourceInstanceIdentifier, identifier, retrievedRevision, bufferAdaptor);
}

template <class DomainType>
DomainType EntityReader<DomainType>::readFromKey(const QByteArray &key) const
{
    auto typeName = ApplicationDomain::getTypeName<DomainType>();
    auto mainDatabase = Storage::mainDatabase(mTransaction, typeName);
    qint64 retrievedRevision = 0;
    auto bufferAdaptor = EntityReaderUtils::get(mainDatabase, key, mDomainTypeAdaptorFactory, retrievedRevision);
    const auto identifier = Storage::uidFromKey(key);
    if (!bufferAdaptor) {
        return DomainType();
    }
    return DomainType(mResourceInstanceIdentifier, identifier, retrievedRevision, bufferAdaptor);
}

template <class DomainType>
DomainType EntityReader<DomainType>::readPrevious(const QByteArray &uid, qint64 revision) const
{
    auto typeName = ApplicationDomain::getTypeName<DomainType>();
    auto mainDatabase = Storage::mainDatabase(mTransaction, typeName);
    qint64 retrievedRevision = 0;
    auto bufferAdaptor = EntityReaderUtils::getPrevious(mainDatabase, uid, revision, mDomainTypeAdaptorFactory, retrievedRevision);
    if (!bufferAdaptor) {
        return DomainType();
    }
    return DomainType(mResourceInstanceIdentifier, uid, retrievedRevision, bufferAdaptor);
}

template <class DomainType>
void EntityReader<DomainType>::query(const Sink::Query &query, const std::function<bool(const DomainType &)> &callback)
{
    executeInitialQuery(query, 0, 0,
        [&callback](const typename DomainType::Ptr &value, Sink::Operation operation) -> bool {
            Q_ASSERT(operation == Sink::Operation_Creation);
            return callback(*value);
        });
}

template <class DomainType>
void EntityReader<DomainType>::readEntity(const Sink::Storage::NamedDatabase &db, const QByteArray &key,
    const std::function<void(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &, Sink::Operation)> &resultCallback)
{
    db.findLatest(key,
        [=](const QByteArray &key, const QByteArray &value) -> bool {
            Sink::EntityBuffer buffer(value.data(), value.size());
            const Sink::Entity &entity = buffer.entity();
            const auto metadataBuffer = Sink::EntityBuffer::readBuffer<Sink::Metadata>(entity.metadata());
            const qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
            const auto operation = metadataBuffer ? metadataBuffer->operation() : Sink::Operation_Creation;
            auto adaptor = mDomainTypeAdaptorFactory.createAdaptor(entity);
            Q_ASSERT(adaptor);
            resultCallback(DomainType::Ptr::create(mResourceInstanceIdentifier, Sink::Storage::uidFromKey(key), revision, adaptor), operation);
            return false;
        },
        [&](const Sink::Storage::Error &error) { SinkWarning() << "Error during query: " << error.message << key; });
}

static inline ResultSet fullScan(const Sink::Storage::Transaction &transaction, const QByteArray &bufferType)
{
    // TODO use a result set with an iterator, to read values on demand
    SinkTrace() << "Looking for : " << bufferType;
    //The scan can return duplicate results if we have multiple revisions, so we use a set to deduplicate.
    QSet<QByteArray> keys;
    Storage::mainDatabase(transaction, bufferType)
        .scan(QByteArray(),
            [&](const QByteArray &key, const QByteArray &value) -> bool {
                if (keys.contains(Sink::Storage::uidFromKey(key))) {
                    //Not something that should persist if the replay works, so we keep a message for now.
                    SinkTrace() << "Multiple revisions for key: " << key;
                }
                keys << Sink::Storage::uidFromKey(key);
                return true;
            },
            [](const Sink::Storage::Error &error) { SinkWarning() << "Error during query: " << error.message; });

    SinkTrace() << "Full scan retrieved " << keys.size() << " results.";
    return ResultSet(keys.toList().toVector());
}

template <class DomainType>
ResultSet EntityReader<DomainType>::loadInitialResultSet(const Sink::Query &query, QSet<QByteArray> &remainingFilters, QByteArray &remainingSorting)
{
    if (!query.ids.isEmpty()) {
        return ResultSet(query.ids.toVector());
    }
    QSet<QByteArray> appliedFilters;
    QByteArray appliedSorting;
    auto resultSet = Sink::ApplicationDomain::TypeImplementation<DomainType>::queryIndexes(query, mResourceInstanceIdentifier, appliedFilters, appliedSorting, mTransaction);
    remainingFilters = query.propertyFilter.keys().toSet() - appliedFilters;
    if (appliedSorting.isEmpty()) {
        remainingSorting = query.sortProperty;
    }

    // We do a full scan if there were no indexes available to create the initial set.
    if (appliedFilters.isEmpty()) {
        // TODO this should be replaced by an index lookup as well
        resultSet = fullScan(mTransaction, ApplicationDomain::getTypeName<DomainType>());
    }
    return resultSet;
}

template <class DomainType>
ResultSet EntityReader<DomainType>::loadIncrementalResultSet(qint64 baseRevision, const Sink::Query &query, QSet<QByteArray> &remainingFilters)
{
    const auto bufferType = ApplicationDomain::getTypeName<DomainType>();
    auto revisionCounter = QSharedPointer<qint64>::create(baseRevision);
    remainingFilters = query.propertyFilter.keys().toSet();
    return ResultSet([this, bufferType, revisionCounter]() -> QByteArray {
        const qint64 topRevision = Sink::Storage::maxRevision(mTransaction);
        // Spit out the revision keys one by one.
        while (*revisionCounter <= topRevision) {
            const auto uid = Sink::Storage::getUidFromRevision(mTransaction, *revisionCounter);
            const auto type = Sink::Storage::getTypeFromRevision(mTransaction, *revisionCounter);
            // SinkTrace() << "Revision" << *revisionCounter << type << uid;
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
        SinkTrace() << "Finished reading incremental result set:" << *revisionCounter;
        // We're done
        return QByteArray();
    });
}

template <class DomainType>
ResultSet EntityReader<DomainType>::filterAndSortSet(ResultSet &resultSet, const std::function<bool(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter,
    const Sink::Storage::NamedDatabase &db, bool initialQuery, const QByteArray &sortProperty)
{
    const bool sortingRequired = !sortProperty.isEmpty();
    if (initialQuery && sortingRequired) {
        SinkTrace() << "Sorting the resultset in memory according to property: " << sortProperty;
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

        SinkTrace() << "Sorted " << sortedMap->size() << " values.";
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
QPair<qint64, qint64> EntityReader<DomainType>::load(const Sink::Query &query, const std::function<ResultSet(QSet<QByteArray> &, QByteArray &)> &baseSetRetriever, bool initialQuery, int offset, int batchSize, const std::function<bool(const typename DomainType::Ptr &value, Sink::Operation operation)> &callback)
{
    QTime time;
    time.start();

    auto db = Storage::mainDatabase(mTransaction, ApplicationDomain::getTypeName<DomainType>());

    QSet<QByteArray> remainingFilters;
    QByteArray remainingSorting;
    auto resultSet = baseSetRetriever(remainingFilters, remainingSorting);
    SinkTrace() << "Base set retrieved. " << Log::TraceTime(time.elapsed());
    auto filteredSet = filterAndSortSet(resultSet, getFilter(remainingFilters, query), db, initialQuery, remainingSorting);
    SinkTrace() << "Filtered set retrieved. " << Log::TraceTime(time.elapsed());
    auto replayedEntities = replaySet(filteredSet, offset, batchSize, callback);
    // SinkTrace() << "Filtered set replayed. " << Log::TraceTime(time.elapsed());
    return qMakePair(Sink::Storage::maxRevision(mTransaction), replayedEntities);
}

template <class DomainType>
QPair<qint64, qint64> EntityReader<DomainType>::executeInitialQuery(const Sink::Query &query, int offset, int batchsize, const std::function<bool(const typename DomainType::Ptr &value, Sink::Operation operation)> &callback)
{
    QTime time;
    time.start();
    auto revisionAndReplayedEntities = load(query, [&](QSet<QByteArray> &remainingFilters, QByteArray &remainingSorting) -> ResultSet {
        return loadInitialResultSet(query, remainingFilters, remainingSorting);
    }, true, offset, batchsize, callback);
    SinkTrace() << "Initial query took: " << Log::TraceTime(time.elapsed());
    return revisionAndReplayedEntities;
}

template <class DomainType>
QPair<qint64, qint64> EntityReader<DomainType>::executeIncrementalQuery(const Sink::Query &query, qint64 lastRevision, const std::function<bool(const typename DomainType::Ptr &value, Sink::Operation operation)> &callback)
{
    QTime time;
    time.start();
    const qint64 baseRevision = lastRevision + 1;
    auto revisionAndReplayedEntities = load(query, [&](QSet<QByteArray> &remainingFilters, QByteArray &remainingSorting) -> ResultSet {
        return loadIncrementalResultSet(baseRevision, query, remainingFilters);
    }, false, 0, 0, callback);
    SinkTrace() << "Initial query took: " << Log::TraceTime(time.elapsed());
    return revisionAndReplayedEntities;
}

template <class DomainType>
std::function<bool(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)>
EntityReader<DomainType>::getFilter(const QSet<QByteArray> remainingFilters, const Sink::Query &query)
{
    return [this, remainingFilters, query](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &domainObject) -> bool {
        if (!query.ids.isEmpty()) {
            if (!query.ids.contains(domainObject->identifier())) {
                return false;
            }
        }
        for (const auto &filterProperty : remainingFilters) {
            const auto property = domainObject->getProperty(filterProperty);
            const auto comparator = query.propertyFilter.value(filterProperty);
            if (!comparator.matches(property)) {
                SinkTrace() << "Filtering entity due to property mismatch on filter: " << filterProperty << property << ":" << comparator.value;
                return false;
            }
        }
        return true;
    };
}

template <class DomainType>
qint64 EntityReader<DomainType>::replaySet(ResultSet &resultSet, int offset, int batchSize, const std::function<bool(const typename DomainType::Ptr &value, Sink::Operation operation)> &callback)
{
    SinkTrace() << "Skipping over " << offset << " results";
    resultSet.skip(offset);
    int counter = 0;
    while (!batchSize || (counter < batchSize)) {
        const bool ret =
            resultSet.next([&counter, callback](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &value, Sink::Operation operation) -> bool {
                counter++;
                return callback(value.staticCast<DomainType>(), operation);
            });
        if (!ret) {
            break;
        }
    };
    SinkTrace() << "Replayed " << counter << " results."
            << "Limit " << batchSize;
    return counter;
}

template class Sink::EntityReader<Sink::ApplicationDomain::Folder>;
template class Sink::EntityReader<Sink::ApplicationDomain::Mail>;
template class Sink::EntityReader<Sink::ApplicationDomain::Event>;
