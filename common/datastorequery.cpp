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
#include "applicationdomaintype.h"

#include "folder.h"
#include "mail.h"
#include "event.h"

using namespace Sink;
using namespace Sink::Storage;


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

    bool next(const std::function<void(const ResultSet::Result &result)> &callback) Q_DECL_OVERRIDE
    {
        if (mIt == mIds.constEnd()) {
            return false;
        }
        readEntity(*mIt, [callback](const QByteArray &uid, const Sink::EntityBuffer &entityBuffer) {
            callback({uid, entityBuffer, entityBuffer.operation()});
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

    bool next(const std::function<void(const ResultSet::Result &result)> &callback) Q_DECL_OVERRIDE
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

    bool next(const std::function<void(const ResultSet::Result &result)> &callback) Q_DECL_OVERRIDE {
        bool foundValue = false;
        while(!foundValue && mSource->next([this, callback, &foundValue](const ResultSet::Result &result) {
                SinkTrace() << "Filter: " << result.uid << result.operation;

                //Always accept removals. They can't match the filter since the data is gone.
                if (result.operation == Sink::Operation_Removal) {
                    SinkTrace() << "Removal: " << result.uid << result.operation;
                    callback(result);
                    foundValue = true;
                } else if (matchesFilter(result.uid, result.buffer)) {
                    SinkTrace() << "Accepted: " << result.uid << result.operation;
                    callback(result);
                    foundValue = true;
                    //TODO if something did not match the filter so far but does now, turn into an add operation.
                } else {
                    SinkTrace() << "Rejected: " << result.uid << result.operation;
                    //TODO emit a removal if we had the uid in the result set and this is a modification.
                    //We don't know if this results in a removal from the dataset, so we emit a removal notification anyways
                    callback({result.uid, result.buffer, Sink::Operation_Removal, result.aggregateValues});
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

    struct Aggregator {
        Query::Reduce::Aggregator::Operation operation;
        QByteArray property;
        QByteArray resultProperty;

        Aggregator(Query::Reduce::Aggregator::Operation o, const QByteArray &property_, const QByteArray &resultProperty_)
            : operation(o), property(property_), resultProperty(resultProperty_)
        {

        }

        void process() {
            if (operation == Query::Reduce::Aggregator::Count) {
                mResult = mResult.toInt() + 1;
            } else {
                Q_ASSERT(false);
            }
        }

        void process(const QVariant &value) {
            if (operation == Query::Reduce::Aggregator::Collect) {
                mResult = mResult.toList() << value;
            } else {
                Q_ASSERT(false);
            }
        }

        QVariant result() const
        {
            return mResult;
        }
    private:
        QVariant mResult;
    };

    QHash<QByteArray, QVariant> mAggregateValues;
    QSet<QByteArray> mReducedValues;
    QByteArray mReductionProperty;
    QByteArray mSelectionProperty;
    Query::Reduce::Selector::Comparator mSelectionComparator;
    QList<Aggregator> mAggregators;

    Reduce(const QByteArray &reductionProperty, const QByteArray &selectionProperty, Query::Reduce::Selector::Comparator comparator, FilterBase::Ptr source, DataStoreQuery *store)
        : FilterBase(source, store),
        mReductionProperty(reductionProperty),
        mSelectionProperty(selectionProperty),
        mSelectionComparator(comparator)
    {

    }

    virtual ~Reduce(){}

    static QByteArray getByteArray(const QVariant &value) {
        if (value.type() == QVariant::DateTime) {
            return value.toDateTime().toString().toLatin1();
        }
        if (value.isValid() && !value.toByteArray().isEmpty()) {
            return value.toByteArray();
        }
        return QByteArray();
    }

    static bool compare(const QVariant &left, const QVariant &right, Query::Reduce::Selector::Comparator comparator) {
        if (comparator == Query::Reduce::Selector::Max) {
            return left > right;
        }
        return false;
    }

    bool next(const std::function<void(const ResultSet::Result &)> &callback) Q_DECL_OVERRIDE {
        bool foundValue = false;
        while(!foundValue && mSource->next([this, callback, &foundValue](const ResultSet::Result &result) {
                auto reductionValue = getProperty(result.buffer.entity(), mReductionProperty);
                if (!mReducedValues.contains(getByteArray(reductionValue))) {
                    //Only reduce every value once.
                    mReducedValues.insert(getByteArray(reductionValue));
                    QVariant selectionResultValue;
                    QByteArray selectionResult;
                    auto results = indexLookup(mReductionProperty, reductionValue);

                    QVariantList list;
                    for (const auto r : results) {
                        readEntity(r, [&, this](const QByteArray &uid, const Sink::EntityBuffer &entityBuffer) {
                            for (auto &aggregator : mAggregators) {
                                if (!aggregator.property.isEmpty()) {
                                    aggregator.process(getProperty(entityBuffer.entity(), aggregator.property));
                                } else {
                                    aggregator.process();
                                }
                            }
                            auto selectionValue = getProperty(entityBuffer.entity(), mSelectionProperty);
                            if (!selectionResultValue.isValid() || compare(selectionValue, selectionResultValue, mSelectionComparator)) {
                                selectionResultValue = selectionValue;
                                selectionResult = uid;
                            }
                        });
                    }

                    QMap<QByteArray, QVariant> aggregateValues;
                    for (auto &aggregator : mAggregators) {
                        aggregateValues.insert(aggregator.resultProperty, aggregator.result());
                    }

                    readEntity(selectionResult, [&, this](const QByteArray &uid, const Sink::EntityBuffer &entityBuffer) {
                        callback({uid, entityBuffer, Sink::Operation_Creation, aggregateValues});
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

    bool next(const std::function<void(const ResultSet::Result &result)> &callback) Q_DECL_OVERRIDE {
        bool foundValue = false;
        while(!foundValue && mSource->next([this, callback, &foundValue](const ResultSet::Result &result) {
                auto bloomValue = getProperty(result.buffer.entity(), mBloomProperty);
                auto results = indexLookup(mBloomProperty, bloomValue);
                for (const auto r : results) {
                    readEntity(r, [&, this](const QByteArray &uid, const Sink::EntityBuffer &entityBuffer) {
                        callback({uid, entityBuffer, Sink::Operation_Creation});
                        foundValue = true;
                    });
                }
                return false;
            }))
        {}
        return foundValue;
    }
};

DataStoreQuery::DataStoreQuery(const Sink::Query &query, const QByteArray &type, EntityStore::Ptr store, TypeIndex &typeIndex, std::function<QVariant(const Sink::Entity &entity, const QByteArray &property)> getProperty)
    : mQuery(query), mType(type), mTypeIndex(typeIndex), mGetProperty(getProperty), mStore(store)
{
    setupQuery();
}

void DataStoreQuery::readEntity(const QByteArray &key, const BufferCallback &resultCallback)
{
    mStore->readLatest(mType, key, [=](const QByteArray &key, const Sink::EntityBuffer &buffer) {
            resultCallback(DataStore::uidFromKey(key), buffer);
            return false;
        });
}

QVariant DataStoreQuery::getProperty(const Sink::Entity &entity, const QByteArray &property)
{
    return mGetProperty(entity, property);
}

QVector<QByteArray> DataStoreQuery::indexLookup(const QByteArray &property, const QVariant &value)
{
    return mStore->indexLookup(mType, property, value);
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

template <typename ... Args>
QSharedPointer<DataStoreQuery> prepareQuery(const QByteArray &type, Args && ... args)
{
    if (type == ApplicationDomain::getTypeName<ApplicationDomain::Folder>()) {
        return ApplicationDomain::TypeImplementation<ApplicationDomain::Folder>::prepareQuery(std::forward<Args>(args)...);
    } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Mail>()) {
        return ApplicationDomain::TypeImplementation<ApplicationDomain::Mail>::prepareQuery(std::forward<Args>(args)...);
    } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Event>()) {
        return ApplicationDomain::TypeImplementation<ApplicationDomain::Event>::prepareQuery(std::forward<Args>(args)...);
    }
    Q_ASSERT(false);
    return QSharedPointer<DataStoreQuery>();
}

QByteArrayList DataStoreQuery::executeSubquery(const Query &subquery)
{
    Q_ASSERT(!subquery.type.isEmpty());
    auto sub = prepareQuery(subquery.type, subquery, mStore);
    auto result = sub->execute();
    QByteArrayList ids;
    while (result.next([&ids](const ResultSet::Result &result) {
            ids << result.uid;
        }))
    {}
    return ids;
}

void DataStoreQuery::setupQuery()
{
    auto baseFilters = mQuery.getBaseFilters();
    for (const auto &k : baseFilters.keys()) {
        const auto comparator = baseFilters.value(k);
        if (comparator.value.canConvert<Query>()) {
            SinkTrace() << "Executing subquery for property: " << k;
            const auto result = executeSubquery(comparator.value.value<Query>());
            baseFilters.insert(k, Query::Comparator(QVariant::fromValue(result), Query::Comparator::In));
        }
    }
    mQuery.setBaseFilters(baseFilters);

    FilterBase::Ptr baseSet;
    QSet<QByteArray> remainingFilters = mQuery.getBaseFilters().keys().toSet();
    QByteArray appliedSorting;
    if (!mQuery.ids().isEmpty()) {
        mSource = Source::Ptr::create(mQuery.ids().toVector(), this);
        baseSet = mSource;
    } else {
        QSet<QByteArray> appliedFilters;

        auto resultSet = mStore->indexLookup(mType, mQuery, appliedFilters, appliedSorting);
        remainingFilters = remainingFilters - appliedFilters;

        // We do a full scan if there were no indexes available to create the initial set.
        if (appliedFilters.isEmpty()) {
            // TODO this should be replaced by an index lookup on the uid index
            mSource = Source::Ptr::create(mStore->fullScan(mType), this);
        } else {
            mSource = Source::Ptr::create(resultSet, this);
        }
        baseSet = mSource;
    }
    if (!mQuery.getBaseFilters().isEmpty()) {
        auto filter = Filter::Ptr::create(baseSet, this);
        //For incremental queries the remaining filters are not sufficient
        for (const auto &f : mQuery.getBaseFilters().keys()) {
            filter->propertyFilter.insert(f, mQuery.getFilter(f));
        }
        baseSet = filter;
    }
    /* if (appliedSorting.isEmpty() && !mQuery.sortProperty.isEmpty()) { */
    /*     //Apply manual sorting */
    /*     baseSet = Sort::Ptr::create(baseSet, mQuery.sortProperty); */
    /* } */

    //Setup the rest of the filter stages on top of the base set
    for (const auto &stage : mQuery.getFilterStages()) {
        if (auto filter = stage.dynamicCast<Query::Filter>()) {
            auto f = Filter::Ptr::create(baseSet, this);
            f->propertyFilter = filter->propertyFilter;
            baseSet = f;
        } else if (auto filter = stage.dynamicCast<Query::Reduce>()) {
            auto reduction = Reduce::Ptr::create(filter->property, filter->selector.property, filter->selector.comparator, baseSet, this);
            for (const auto &aggregator : filter->aggregators) {
                reduction->mAggregators << Reduce::Aggregator(aggregator.operation, aggregator.propertyToCollect, aggregator.resultProperty);
            }
            baseSet = reduction;
        } else if (auto filter = stage.dynamicCast<Query::Bloom>()) {
            baseSet = Bloom::Ptr::create(filter->property, baseSet, this);
        }
    }

    mCollector = Collector::Ptr::create(baseSet, this);
}

QVector<QByteArray> DataStoreQuery::loadIncrementalResultSet(qint64 baseRevision)
{
    auto revisionCounter = QSharedPointer<qint64>::create(baseRevision);
    QVector<QByteArray> changedKeys;
    mStore->readRevisions(baseRevision, mType, [&](const QByteArray &key) {
        changedKeys << key;
    });
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
        if (mCollector->next([this, callback](const ResultSet::Result &result) {
                SinkTrace() << "Got incremental result: " << result.uid << result.operation;
                callback(result);
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
        if (mCollector->next([this, callback](const ResultSet::Result &result) {
                if (result.operation != Sink::Operation_Removal) {
                    SinkTrace() << "Got initial result: " << result.uid << result.operation;
                    callback(ResultSet::Result{result.uid, result.buffer, Sink::Operation_Creation, result.aggregateValues});
                }
            }))
        {
            return true;
        }
        return false;
    };
    return ResultSet(generator, [this]() { mCollector->skip(); });
}
