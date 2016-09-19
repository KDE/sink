/*
 *   Copyright (C) 2016 Christian Mollekopf <chrigi_1@fastmail.fm>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */
#include "datastorequery.h"

#include "log.h"
#include "entitybuffer.h"
#include "entity_generated.h"

using namespace Sink;


SINK_DEBUG_AREA("datastorequery")

DataStoreQuery::DataStoreQuery(const Sink::Query &query, const QByteArray &type, Sink::Storage::Transaction &transaction, TypeIndex &typeIndex, std::function<QVariant(const Sink::Entity &entity, const QByteArray &property)> getProperty)
    : mQuery(query), mTransaction(transaction), mType(type), mTypeIndex(typeIndex), mDb(Storage::mainDatabase(mTransaction, mType)), mGetProperty(getProperty)
{

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

ResultSet DataStoreQuery::loadInitialResultSet(QSet<QByteArray> &remainingFilters, QByteArray &remainingSorting)
{
    if (!mQuery.ids.isEmpty()) {
        return ResultSet(mQuery.ids.toVector());
    }
    QSet<QByteArray> appliedFilters;
    QByteArray appliedSorting;

    auto resultSet = mTypeIndex.query(mQuery, appliedFilters, appliedSorting, mTransaction);

    remainingFilters = mQuery.propertyFilter.keys().toSet() - appliedFilters;
    if (appliedSorting.isEmpty()) {
        remainingSorting = mQuery.sortProperty;
    }

    // We do a full scan if there were no indexes available to create the initial set.
    if (appliedFilters.isEmpty()) {
        // TODO this should be replaced by an index lookup as well
        resultSet = fullScan(mTransaction, mType);
    }
    return resultSet;
}

ResultSet DataStoreQuery::loadIncrementalResultSet(qint64 baseRevision, QSet<QByteArray> &remainingFilters)
{
    const auto bufferType = mType;
    auto revisionCounter = QSharedPointer<qint64>::create(baseRevision);
    remainingFilters = mQuery.propertyFilter.keys().toSet();
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

void DataStoreQuery::readEntity(const QByteArray &key, const BufferCallback &resultCallback)
{
    mDb.findLatest(key,
        [=](const QByteArray &key, const QByteArray &value) -> bool {
            resultCallback(Sink::Storage::uidFromKey(key), Sink::EntityBuffer(value.data(), value.size()));
            return false;
        },
        [&](const Sink::Storage::Error &error) { SinkWarning() << "Error during query: " << error.message << key; });
}

QVariant DataStoreQuery::getProperty(const Sink::Entity &entity, const QByteArray &property)
{
    return mGetProperty(entity, property);
}

ResultSet DataStoreQuery::filterAndSortSet(ResultSet &resultSet, const FilterFunction &filter, bool initialQuery, const QByteArray &sortProperty)
{
    const bool sortingRequired = !sortProperty.isEmpty();
    if (initialQuery && sortingRequired) {
        SinkTrace() << "Sorting the resultset in memory according to property: " << sortProperty;
        // Sort the complete set by reading the sort property and filling into a sorted map
        auto sortedMap = QSharedPointer<QMap<QByteArray, QByteArray>>::create();
        while (resultSet.next()) {
            // readEntity is only necessary if we actually want to filter or know the operation type (but not a big deal if we do it always I guess)
            readEntity(resultSet.id(),
                [this, filter, initialQuery, sortedMap, sortProperty, &resultSet](const QByteArray &uid, const Sink::EntityBuffer &buffer) {

                    const auto operation = buffer.operation();

                    // We're not interested in removals during the initial query
                    if ((operation != Sink::Operation_Removal) && filter(uid, buffer)) {
                        if (!sortProperty.isEmpty()) {
                            const auto sortValue = getProperty(buffer.entity(), sortProperty);
                            if (sortValue.type() == QVariant::DateTime) {
                                sortedMap->insert(QByteArray::number(std::numeric_limits<unsigned int>::max() - sortValue.toDateTime().toTime_t()), uid);
                            } else {
                                sortedMap->insert(sortValue.toString().toLatin1(), uid);
                            }
                        } else {
                            sortedMap->insert(uid, uid);
                        }
                    }
                });
        }

        SinkTrace() << "Sorted " << sortedMap->size() << " values.";
        auto iterator = QSharedPointer<QMapIterator<QByteArray, QByteArray>>::create(*sortedMap);
        ResultSet::ValueGenerator generator = [this, iterator, sortedMap, filter, initialQuery](
            std::function<void(const QByteArray &uid, const Sink::EntityBuffer &entity, Sink::Operation)> callback) -> bool {
            if (iterator->hasNext()) {
                readEntity(iterator->next().value(), [this, filter, callback, initialQuery](const QByteArray &uid, const Sink::EntityBuffer &buffer) {
                        callback(uid, buffer, Sink::Operation_Creation);
                    });
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
        ResultSet::ValueGenerator generator = [this, resultSetPtr, filter, initialQuery](const ResultSet::Callback &callback) -> bool {
            if (resultSetPtr->next()) {
                SinkTrace() << "Reading the next value: " << resultSetPtr->id();
                // readEntity is only necessary if we actually want to filter or know the operation type (but not a big deal if we do it always I guess)
                readEntity(resultSetPtr->id(), [this, filter, callback, initialQuery](const QByteArray &uid, const Sink::EntityBuffer &buffer) {
                    const auto operation = buffer.operation();
                    if (initialQuery) {
                        // We're not interested in removals during the initial query
                        if ((operation != Sink::Operation_Removal) && filter(uid, buffer)) {
                            // In the initial set every entity is new
                            callback(uid, buffer, Sink::Operation_Creation);
                        }
                    } else {
                        // Always remove removals, they probably don't match due to non-available properties
                        if ((operation == Sink::Operation_Removal) || filter(uid, buffer)) {
                            // TODO only replay if this is in the currently visible set (or just always replay, worst case we have a couple to many results)
                            callback(uid, buffer, operation);
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


DataStoreQuery::FilterFunction DataStoreQuery::getFilter(const QSet<QByteArray> &remainingFilters)
{
    auto query = mQuery;
    return [this, remainingFilters, query](const QByteArray &uid, const Sink::EntityBuffer &entity) -> bool {
        if (!query.ids.isEmpty()) {
            if (!query.ids.contains(uid)) {
                SinkTrace() << "Filter by uid: " << uid;
                return false;
            }
        }
        for (const auto &filterProperty : remainingFilters) {
            const auto property = getProperty(entity.entity(), filterProperty);
            const auto comparator = query.propertyFilter.value(filterProperty);
            if (!comparator.matches(property)) {
                SinkTrace() << "Filtering entity due to property mismatch on filter: " << filterProperty << property << ":" << comparator.value;
                return false;
            }
        }
        return true;
    };
}

ResultSet DataStoreQuery::update(qint64 baseRevision)
{
    SinkTrace() << "Executing query update";
    QSet<QByteArray> remainingFilters;
    QByteArray remainingSorting;
    auto resultSet = loadIncrementalResultSet(baseRevision, remainingFilters);
    auto filteredSet = filterAndSortSet(resultSet, getFilter(remainingFilters), false, remainingSorting);
    return filteredSet;
}

ResultSet DataStoreQuery::execute()
{
    SinkTrace() << "Executing query";
    QSet<QByteArray> remainingFilters;
    QByteArray remainingSorting;
    auto resultSet = loadInitialResultSet(remainingFilters, remainingSorting);
    auto filteredSet = filterAndSortSet(resultSet, getFilter(remainingFilters), true, remainingSorting);
    return filteredSet;
}
