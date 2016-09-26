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

class Source : public FilterBase {
    public:
    typedef QSharedPointer<Source> Ptr;

    QVector<QByteArray> mIds;
    QVector<QByteArray>::ConstIterator mIt;

    Source (const QVector<QByteArray> &ids, DataStoreQuery *store)
        : FilterBase(store),
        mIds(ids),
        mIt(mIds.constBegin())
    {

    }

    virtual ~Source(){}

    virtual void skip() Q_DECL_OVERRIDE
    {
        if (mIt != mIds.constEnd()) {
            mIt++;
        }
    };

    void add(const QVector<QByteArray> &ids)
    {
        mIds = ids;
        mIt = mIds.constBegin();
    }

    bool next(const std::function<void(Sink::Operation operation, const QByteArray &uid, const Sink::EntityBuffer &entityBuffer)> &callback) Q_DECL_OVERRIDE
    {
        if (mIt == mIds.constEnd()) {
            return false;
        }
        readEntity(*mIt, [callback](const QByteArray &uid, const Sink::EntityBuffer &entityBuffer) {
            callback(entityBuffer.operation(), uid, entityBuffer);
        });
        mIt++;
        return mIt != mIds.constEnd();
    }
};

class Collector : public FilterBase {
public:
    typedef QSharedPointer<Collector> Ptr;

    Collector(FilterBase::Ptr source, DataStoreQuery *store)
        : FilterBase(source, store)
    {

    }
    virtual ~Collector(){}

    bool next(const std::function<void(Sink::Operation operation, const QByteArray &uid, const Sink::EntityBuffer &entityBuffer)> &callback) Q_DECL_OVERRIDE
    {
        return mSource->next(callback);
    }
};

class Filter : public FilterBase {
public:
    typedef QSharedPointer<Filter> Ptr;

    QHash<QByteArray, Sink::Query::Comparator> propertyFilter;

    Filter(FilterBase::Ptr source, DataStoreQuery *store)
        : FilterBase(source, store)
    {

    }

    virtual ~Filter(){}

    bool next(const std::function<void(Sink::Operation operation, const QByteArray &uid, const Sink::EntityBuffer &entityBuffer)> &callback) Q_DECL_OVERRIDE {
        bool foundValue = false;
        while(!foundValue && mSource->next([this, callback, &foundValue](Sink::Operation operation, const QByteArray &uid, const Sink::EntityBuffer &entityBuffer) {
                SinkTrace() << "Filter: " << uid << operation;

                //Always accept removals. They can't match the filter since the data is gone.
                if (operation == Sink::Operation_Removal) {
                    SinkTrace() << "Removal: " << uid << operation;
                    callback(operation, uid, entityBuffer);
                    foundValue = true;
                } else if (matchesFilter(uid, entityBuffer)) {
                    SinkTrace() << "Accepted: " << uid << operation;
                    callback(operation, uid, entityBuffer);
                    foundValue = true;
                    //TODO if something did not match the filter so far but does now, turn into an add operation.
                } else {
                    SinkTrace() << "Rejected: " << uid << operation;
                    //TODO emit a removal if we had the uid in the result set and this is a modification.
                    //We don't know if this results in a removal from the dataset, so we emit a removal notification anyways
                    callback(Sink::Operation_Removal, uid, entityBuffer);
                }
                return false;
            }))
        {}
        return foundValue;
    }

    bool matchesFilter(const QByteArray &uid, const Sink::EntityBuffer &entityBuffer) {
        for (const auto &filterProperty : propertyFilter.keys()) {
            const auto property = getProperty(entityBuffer.entity(), filterProperty);
            const auto comparator = propertyFilter.value(filterProperty);
            if (!comparator.matches(property)) {
                SinkTrace() << "Filtering entity due to property mismatch on filter: " << filterProperty << property << ":" << comparator.value;
                return false;
            }
        }
        return true;
    }
};

class Reduce : public FilterBase {
public:
    typedef QSharedPointer<Reduce> Ptr;

    QHash<QByteArray, QVariant> mAggregateValues;
    QByteArray mReductionProperty;
    QByteArray mSelectionProperty;
    Query::Reduce::Selector::Comparator mSelectionComparator;

    Reduce(const QByteArray &reductionProperty, const QByteArray &selectionProperty, Query::Reduce::Selector::Comparator comparator, FilterBase::Ptr source, DataStoreQuery *store)
        : FilterBase(source, store),
        mReductionProperty(reductionProperty),
        mSelectionProperty(selectionProperty),
        mSelectionComparator(comparator)
    {

    }

    virtual ~Reduce(){}

    static QByteArray getByteArray(const QVariant &value)
    {
        if (value.type() == QVariant::DateTime) {
            return value.toDateTime().toString().toLatin1();
        }
        if (value.isValid() && !value.toByteArray().isEmpty()) {
            return value.toByteArray();
        }
        return QByteArray();
    }

    static bool compare(const QVariant &left, const QVariant &right, Query::Reduce::Selector::Comparator comparator)
    {
        if (comparator == Query::Reduce::Selector::Max) {
            return left > right;
        }
        return false;
    }

    bool next(const std::function<void(Sink::Operation operation, const QByteArray &uid, const Sink::EntityBuffer &entityBuffer)> &callback) Q_DECL_OVERRIDE {
        bool foundValue = false;
        while(!foundValue && mSource->next([this, callback, &foundValue](Sink::Operation operation, const QByteArray &uid, const Sink::EntityBuffer &entityBuffer) {
                auto reductionValue = getProperty(entityBuffer.entity(), mReductionProperty);
                if (!mAggregateValues.contains(getByteArray(reductionValue))) {
                    QVariant selectionResultValue;
                    QByteArray selectionResult;
                    auto results = indexLookup(mReductionProperty, reductionValue);
                    for (const auto r : results) {
                        readEntity(r, [&, this](const QByteArray &uid, const Sink::EntityBuffer &entityBuffer) {
                            auto selectionValue = getProperty(entityBuffer.entity(), mSelectionProperty);
                            if (!selectionResultValue.isValid() || compare(selectionValue, selectionResultValue, mSelectionComparator)) {
                                selectionResultValue = selectionValue;
                                selectionResult = uid;
                            }
                        });
                    }
                    readEntity(selectionResult, [&, this](const QByteArray &uid, const Sink::EntityBuffer &entityBuffer) {
                        callback(Sink::Operation_Creation, uid, entityBuffer);
                        foundValue = true;
                    });
                }
                return false;
            }))
        {}
        return foundValue;
    }
};

class Bloom : public FilterBase {
public:
    typedef QSharedPointer<Bloom> Ptr;

    QByteArray mBloomProperty;

    Bloom(const QByteArray &bloomProperty, FilterBase::Ptr source, DataStoreQuery *store)
        : FilterBase(source, store),
        mBloomProperty(bloomProperty)
    {

    }

    virtual ~Bloom(){}

    bool next(const std::function<void(Sink::Operation operation, const QByteArray &uid, const Sink::EntityBuffer &entityBuffer)> &callback) Q_DECL_OVERRIDE {
        bool foundValue = false;
        while(!foundValue && mSource->next([this, callback, &foundValue](Sink::Operation operation, const QByteArray &uid, const Sink::EntityBuffer &entityBuffer) {
                auto bloomValue = getProperty(entityBuffer.entity(), mBloomProperty);
                auto results = indexLookup(mBloomProperty, bloomValue);
                for (const auto r : results) {
                    readEntity(r, [&, this](const QByteArray &uid, const Sink::EntityBuffer &entityBuffer) {
                        callback(Sink::Operation_Creation, uid, entityBuffer);
                        foundValue = true;
                    });
                }
                return false;
            }))
        {}
        return foundValue;
    }
};

DataStoreQuery::DataStoreQuery(const Sink::Query &query, const QByteArray &type, Sink::Storage::Transaction &transaction, TypeIndex &typeIndex, std::function<QVariant(const Sink::Entity &entity, const QByteArray &property)> getProperty)
    : mQuery(query), mTransaction(transaction), mType(type), mTypeIndex(typeIndex), mDb(Storage::mainDatabase(mTransaction, mType)), mGetProperty(getProperty)
{
    setupQuery();
}

static inline QVector<QByteArray> fullScan(const Sink::Storage::Transaction &transaction, const QByteArray &bufferType)
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
    return keys.toList().toVector();
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

QVector<QByteArray> DataStoreQuery::indexLookup(const QByteArray &property, const QVariant &value)
{
    return mTypeIndex.lookup(property, value, mTransaction);
}

/* ResultSet DataStoreQuery::filterAndSortSet(ResultSet &resultSet, const FilterFunction &filter, const QByteArray &sortProperty) */
/* { */
/*     const bool sortingRequired = !sortProperty.isEmpty(); */
/*     if (mInitialQuery && sortingRequired) { */
/*         SinkTrace() << "Sorting the resultset in memory according to property: " << sortProperty; */
/*         // Sort the complete set by reading the sort property and filling into a sorted map */
/*         auto sortedMap = QSharedPointer<QMap<QByteArray, QByteArray>>::create(); */
/*         while (resultSet.next()) { */
/*             // readEntity is only necessary if we actually want to filter or know the operation type (but not a big deal if we do it always I guess) */
/*             readEntity(resultSet.id(), */
/*                 [this, filter, sortedMap, sortProperty, &resultSet](const QByteArray &uid, const Sink::EntityBuffer &buffer) { */

/*                     const auto operation = buffer.operation(); */

/*                     // We're not interested in removals during the initial query */
/*                     if ((operation != Sink::Operation_Removal) && filter(uid, buffer)) { */
/*                         if (!sortProperty.isEmpty()) { */
/*                             const auto sortValue = getProperty(buffer.entity(), sortProperty); */
/*                             if (sortValue.type() == QVariant::DateTime) { */
/*                                 sortedMap->insert(QByteArray::number(std::numeric_limits<unsigned int>::max() - sortValue.toDateTime().toTime_t()), uid); */
/*                             } else { */
/*                                 sortedMap->insert(sortValue.toString().toLatin1(), uid); */
/*                             } */
/*                         } else { */
/*                             sortedMap->insert(uid, uid); */
/*                         } */
/*                     } */
/*                 }); */
/*         } */

/*         SinkTrace() << "Sorted " << sortedMap->size() << " values."; */
/*         auto iterator = QSharedPointer<QMapIterator<QByteArray, QByteArray>>::create(*sortedMap); */
/*         ResultSet::ValueGenerator generator = [this, iterator, sortedMap, filter]( */
/*             std::function<void(const QByteArray &uid, const Sink::EntityBuffer &entity, Sink::Operation)> callback) -> bool { */
/*             if (iterator->hasNext()) { */
/*                 readEntity(iterator->next().value(), [this, filter, callback](const QByteArray &uid, const Sink::EntityBuffer &buffer) { */
/*                         callback(uid, buffer, Sink::Operation_Creation); */
/*                     }); */
/*                 return true; */
/*             } */
/*             return false; */
/*         }; */

/*         auto skip = [iterator]() { */
/*             if (iterator->hasNext()) { */
/*                 iterator->next(); */
/*             } */
/*         }; */
/*         return ResultSet(generator, skip); */
/*     } else { */
/*         auto resultSetPtr = QSharedPointer<ResultSet>::create(resultSet); */
/*         ResultSet::ValueGenerator generator = [this, resultSetPtr, filter](const ResultSet::Callback &callback) -> bool { */
/*             if (resultSetPtr->next()) { */
/*                 SinkTrace() << "Reading the next value: " << resultSetPtr->id(); */
/*                 // readEntity is only necessary if we actually want to filter or know the operation type (but not a big deal if we do it always I guess) */
/*                 readEntity(resultSetPtr->id(), [this, filter, callback](const QByteArray &uid, const Sink::EntityBuffer &buffer) { */
/*                     const auto operation = buffer.operation(); */
/*                     if (mInitialQuery) { */
/*                         // We're not interested in removals during the initial query */
/*                         if ((operation != Sink::Operation_Removal) && filter(uid, buffer)) { */
/*                             // In the initial set every entity is new */
/*                             callback(uid, buffer, Sink::Operation_Creation); */
/*                         } */
/*                     } else { */
/*                         // Always remove removals, they probably don't match due to non-available properties */
/*                         if ((operation == Sink::Operation_Removal) || filter(uid, buffer)) { */
/*                             // TODO only replay if this is in the currently visible set (or just always replay, worst case we have a couple to many results) */
/*                             callback(uid, buffer, operation); */
/*                         } */
/*                     } */
/*                 }); */
/*                 return true; */
/*             } */
/*             return false; */
/*         }; */
/*         auto skip = [resultSetPtr]() { resultSetPtr->skip(1); }; */
/*         return ResultSet(generator, skip); */
/*     } */
/* } */

void DataStoreQuery::setupQuery()
{
    FilterBase::Ptr baseSet;
    QSet<QByteArray> remainingFilters;
    QByteArray appliedSorting;
    if (!mQuery.ids.isEmpty()) {
        mSource = Source::Ptr::create(mQuery.ids.toVector(), this);
        baseSet = mSource;
        remainingFilters = mQuery.propertyFilter.keys().toSet();
    } else {
        QSet<QByteArray> appliedFilters;

        auto resultSet = mTypeIndex.query(mQuery, appliedFilters, appliedSorting, mTransaction);
        remainingFilters = mQuery.propertyFilter.keys().toSet() - appliedFilters;

        // We do a full scan if there were no indexes available to create the initial set.
        if (appliedFilters.isEmpty()) {
            // TODO this should be replaced by an index lookup on the uid index
            mSource = Source::Ptr::create(fullScan(mTransaction, mType), this);
        } else {
            mSource = Source::Ptr::create(resultSet, this);
        }
        baseSet = mSource;
    }
    if (!mQuery.propertyFilter.isEmpty()) {
        auto filter = Filter::Ptr::create(baseSet, this);
        filter->propertyFilter = mQuery.propertyFilter;
        /* for (const auto &f : remainingFilters) { */
        /*     filter->propertyFilter.insert(f, mQuery.propertyFilter.value(f)); */
        /* } */
        baseSet = filter;
    }
    /* if (appliedSorting.isEmpty() && !mQuery.sortProperty.isEmpty()) { */
    /*     //Apply manual sorting */
    /*     baseSet = Sort::Ptr::create(baseSet, mQuery.sortProperty); */
    /* } */

    for (const auto &stage : mQuery.filterStages) {
        if (auto filter = stage.dynamicCast<Query::Filter>()) {

        } else if (auto filter = stage.dynamicCast<Query::Reduce>()) {
            auto reduce = Reduce::Ptr::create(filter->property, filter->selector.property, filter->selector.comparator, baseSet, this);
            baseSet = reduce;
        } else if (auto filter = stage.dynamicCast<Query::Bloom>()) {
            auto reduce = Bloom::Ptr::create(filter->property, baseSet, this);
            baseSet = reduce;
        }
    }

    mCollector = Collector::Ptr::create(baseSet, this);
}

QVector<QByteArray> DataStoreQuery::loadIncrementalResultSet(qint64 baseRevision)
{
    const auto bufferType = mType;
    auto revisionCounter = QSharedPointer<qint64>::create(baseRevision);
    QVector<QByteArray> changedKeys;
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
        changedKeys << key;
    }
    SinkTrace() << "Finished reading incremental result set:" << *revisionCounter;
    return changedKeys;
}


ResultSet DataStoreQuery::update(qint64 baseRevision)
{
    SinkTrace() << "Executing query update";
    auto incrementalResultSet = loadIncrementalResultSet(baseRevision);
    SinkTrace() << "Changed: " << incrementalResultSet;
    mSource->add(incrementalResultSet);
    ResultSet::ValueGenerator generator = [this](const ResultSet::Callback &callback) -> bool {
        if (mCollector->next([callback](Sink::Operation operation, const QByteArray &uid, const Sink::EntityBuffer &buffer) {
                SinkTrace() << "Got incremental result: " << uid << operation;
                callback(uid, buffer, operation);
            }))
        {
            return true;
        }
        return false;
    };
    return ResultSet(generator, [this]() { mCollector->skip(); });
}


ResultSet DataStoreQuery::execute()
{
    SinkTrace() << "Executing query";

    ResultSet::ValueGenerator generator = [this](const ResultSet::Callback &callback) -> bool {
        if (mCollector->next([callback](Sink::Operation operation, const QByteArray &uid, const Sink::EntityBuffer &buffer) {
                if (operation != Sink::Operation_Removal) {
                    SinkTrace() << "Got initial result: " << uid << operation;
                    callback(uid, buffer, Sink::Operation_Creation);
                }
            }))
        {
            return true;
        }
        return false;
    };
    return ResultSet(generator, [this]() { mCollector->skip(); });
}
