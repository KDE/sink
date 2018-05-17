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
#include "applicationdomaintype.h"

using namespace Sink;
using namespace Sink::Storage;

static QByteArray operationName(const Sink::Operation op)
{
    switch(op) {
        case Sink::Operation_Creation:
            return "Creation";
        case Sink::Operation_Modification:
            return "Modification";
        case Sink::Operation_Removal:
            return "Removal";
    }
    return "";
}

class Source : public FilterBase {
    public:
    typedef QSharedPointer<Source> Ptr;

    QVector<QByteArray> mIds;
    QVector<QByteArray>::ConstIterator mIt;
    QVector<QByteArray> mIncrementalIds;
    QVector<QByteArray>::ConstIterator mIncrementalIt;

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
        mIncrementalIds = ids;
        mIncrementalIt = mIncrementalIds.constBegin();
    }

    bool next(const std::function<void(const ResultSet::Result &result)> &callback) Q_DECL_OVERRIDE
    {
        if (!mIncrementalIds.isEmpty()) {
            if (mIncrementalIt == mIncrementalIds.constEnd()) {
                return false;
            }
            readEntity(*mIncrementalIt, [this, callback](const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Operation operation) {
                SinkTraceCtx(mDatastore->mLogCtx) << "Source: Read entity: " << entity.identifier() << operationName(operation);
                callback({entity, operation});
            });
            mIncrementalIt++;
            if (mIncrementalIt == mIncrementalIds.constEnd()) {
                return false;
            }
            return true;
        } else {
            if (mIt == mIds.constEnd()) {
                return false;
            }
            readEntity(*mIt, [this, callback](const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Operation operation) {
                SinkTraceCtx(mDatastore->mLogCtx) << "Source: Read entity: " << entity.identifier() << operationName(operation);
                callback({entity, operation});
            });
            mIt++;
            return mIt != mIds.constEnd();
        }
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

    QHash<QByteArray, Sink::QueryBase::Comparator> propertyFilter;

    Filter(FilterBase::Ptr source, DataStoreQuery *store)
        : FilterBase(source, store)
    {

    }

    virtual ~Filter(){}

    virtual bool next(const std::function<void(const ResultSet::Result &result)> &callback) Q_DECL_OVERRIDE {
        bool foundValue = false;
        while(!foundValue && mSource->next([this, callback, &foundValue](const ResultSet::Result &result) {
                SinkTraceCtx(mDatastore->mLogCtx) << "Filter: " << result.entity.identifier() << operationName(result.operation);

                //Always accept removals. They can't match the filter since the data is gone.
                if (result.operation == Sink::Operation_Removal) {
                    SinkTraceCtx(mDatastore->mLogCtx) << "Removal: " << result.entity.identifier() << operationName(result.operation);
                    callback(result);
                    foundValue = true;
                } else if (matchesFilter(result.entity)) {
                    SinkTraceCtx(mDatastore->mLogCtx) << "Accepted: " << result.entity.identifier() << operationName(result.operation);
                    callback(result);
                    foundValue = true;
                    //TODO if something did not match the filter so far but does now, turn into an add operation.
                } else {
                    SinkTraceCtx(mDatastore->mLogCtx) << "Rejected: " << result.entity.identifier() << operationName(result.operation);
                    //TODO emit a removal if we had the uid in the result set and this is a modification.
                    //We don't know if this results in a removal from the dataset, so we emit a removal notification anyways
                    callback({result.entity, Sink::Operation_Removal, result.aggregateValues});
                }
                return false;
            }))
        {}
        return foundValue;
    }

    bool matchesFilter(const ApplicationDomain::ApplicationDomainType &entity) {
        for (const auto &filterProperty : propertyFilter.keys()) {
            const auto property = entity.getProperty(filterProperty);
            const auto comparator = propertyFilter.value(filterProperty);
            //We can't deal with a fulltext filter
            if (comparator.comparator == QueryBase::Comparator::Fulltext) {
                continue;
            }
            if (!comparator.matches(property)) {
                SinkTraceCtx(mDatastore->mLogCtx) << "Filtering entity due to property mismatch on filter: " << entity.identifier() << "Property: " << filterProperty << property << " Filter:" << comparator.value;
                return false;
            }
        }
        return true;
    }
};

class Reduce : public Filter {
public:
    typedef QSharedPointer<Reduce> Ptr;

    struct Aggregator {
        QueryBase::Reduce::Aggregator::Operation operation;
        QByteArray property;
        QByteArray resultProperty;

        Aggregator(QueryBase::Reduce::Aggregator::Operation o, const QByteArray &property_, const QByteArray &resultProperty_)
            : operation(o), property(property_), resultProperty(resultProperty_)
        {

        }

        void process(const QVariant &value) {
            if (operation == QueryBase::Reduce::Aggregator::Collect) {
                mResult = mResult.toList() << value;
            } else if (operation == QueryBase::Reduce::Aggregator::Count) {
                mResult = mResult.toInt() + 1;
            } else {
                Q_ASSERT(false);
            }
        }

        void reset()
        {
            mResult.clear();
        }

        QVariant result() const
        {
            return mResult;
        }
    private:
        QVariant mResult;
    };

    QSet<QByteArray> mReducedValues;
    QSet<QByteArray> mIncrementallyReducedValues;
    QHash<QByteArray, QByteArray> mSelectedValues;
    QByteArray mReductionProperty;
    QByteArray mSelectionProperty;
    QueryBase::Reduce::Selector::Comparator mSelectionComparator;
    QList<Aggregator> mAggregators;

    Reduce(const QByteArray &reductionProperty, const QByteArray &selectionProperty, QueryBase::Reduce::Selector::Comparator comparator, FilterBase::Ptr source, DataStoreQuery *store)
        : Filter(source, store),
        mReductionProperty(reductionProperty),
        mSelectionProperty(selectionProperty),
        mSelectionComparator(comparator)
    {

    }

    virtual ~Reduce(){}

    void updateComplete() Q_DECL_OVERRIDE
    {
        mIncrementallyReducedValues.clear();
    }

    static QByteArray getByteArray(const QVariant &value) {
        if (value.type() == QVariant::DateTime) {
            return value.toDateTime().toString().toLatin1();
        }
        if (value.isValid() && !value.toByteArray().isEmpty()) {
            return value.toByteArray();
        }
        return QByteArray();
    }

    static bool compare(const QVariant &left, const QVariant &right, QueryBase::Reduce::Selector::Comparator comparator) {
        if (comparator == QueryBase::Reduce::Selector::Max) {
            return left > right;
        }
        return false;
    }

    struct ReductionResult {
        QByteArray selection;
        QVector<QByteArray> aggregateIds;
        QMap<QByteArray, QVariant> aggregateValues;
    };

    ReductionResult reduceOnValue(const QVariant &reductionValue)
    {
        QMap<QByteArray, QVariant> aggregateValues;
        QVariant selectionResultValue;
        QByteArray selectionResult;
        const auto results = indexLookup(mReductionProperty, reductionValue);
        for (auto &aggregator : mAggregators) {
            aggregator.reset();
        }
        QVector<QByteArray> reducedAndFilteredResults;
        for (const auto &r : results) {
            readEntity(r, [&, this](const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Operation operation) {
                //We need to apply all property filters that we have until the reduction, because the index lookup was unfiltered.
                if (!matchesFilter(entity)) {
                    return;
                }
                reducedAndFilteredResults << r;
                Q_ASSERT(operation != Sink::Operation_Removal);
                for (auto &aggregator : mAggregators) {
                    if (!aggregator.property.isEmpty()) {
                        aggregator.process(entity.getProperty(aggregator.property));
                    } else {
                        aggregator.process(QVariant{});
                    }
                }
                auto selectionValue = entity.getProperty(mSelectionProperty);
                if (!selectionResultValue.isValid() || compare(selectionValue, selectionResultValue, mSelectionComparator)) {
                    selectionResultValue = selectionValue;
                    selectionResult = entity.identifier();
                }
            });
        }

        for (auto &aggregator : mAggregators) {
            aggregateValues.insert(aggregator.resultProperty, aggregator.result());
        }
        return {selectionResult, reducedAndFilteredResults, aggregateValues};
    }

    bool next(const std::function<void(const ResultSet::Result &)> &callback) Q_DECL_OVERRIDE {
        bool foundValue = false;
        while(!foundValue && mSource->next([this, callback, &foundValue](const ResultSet::Result &result) {
                const auto reductionValue = [&] {
                    const auto v = result.entity.getProperty(mReductionProperty);
                    //Because we also get Operation_Removal for filtered entities. We use the fact that actually removed entites
                    //won't have the property to reduce on.
                    //TODO: Perhaps find a cleaner solutoin than abusing Operation::Removed for filtered properties.
                    if (v.isNull() && result.operation == Sink::Operation_Removal) {
                        //For removals we have to read the last revision to get a value, and thus be able to find the correct thread.
                        QVariant reductionValue;
                        readPrevious(result.entity.identifier(), [&] (const ApplicationDomain::ApplicationDomainType &prev) {
                            reductionValue = prev.getProperty(mReductionProperty);
                        });
                        return reductionValue;
                    } else {
                        return v;
                    }
                }();
                if (reductionValue.isNull()) {
                    //We failed to find a value to reduce on, so ignore this entity.
                    //Can happen if the entity was already removed and we have no previous revision.
                    return;
                }
                const auto reductionValueBa = getByteArray(reductionValue);
                if (!mReducedValues.contains(reductionValueBa)) {
                    //Only reduce every value once.
                    mReducedValues.insert(reductionValueBa);
                    auto reductionResult = reduceOnValue(reductionValue);

                    //This can happen if we get a removal message from a filtered entity and all entites of the reduction are filtered.
                    if (reductionResult.selection.isEmpty()) {
                        return;
                    }
                    mSelectedValues.insert(reductionValueBa, reductionResult.selection);
                    readEntity(reductionResult.selection, [&](const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Operation operation) {
                        callback({entity, operation, reductionResult.aggregateValues, reductionResult.aggregateIds});
                        foundValue = true;
                    });
                } else {
                    //During initial query, do nothing. The lookup above will take care of it.
                    //During updates adjust the reduction according to the modification/addition or removal
                    //We have to redo the reduction for every element, because of the aggregation values.
                    if (mIncremental && !mIncrementallyReducedValues.contains(reductionValueBa)) {
                        mIncrementallyReducedValues.insert(reductionValueBa);
                        //Redo the reduction to find new aggregated values
                        auto selectionResult = reduceOnValue(reductionValue);

                        //TODO if old and new are the same a modification would be enough
                        auto oldSelectionResult = mSelectedValues.take(reductionValueBa);
                        if (oldSelectionResult == selectionResult.selection) {
                            mSelectedValues.insert(reductionValueBa, selectionResult.selection);
                            Q_ASSERT(!selectionResult.selection.isEmpty());
                            readEntity(selectionResult.selection, [&](const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Operation) {
                                callback({entity, Sink::Operation_Modification, selectionResult.aggregateValues, selectionResult.aggregateIds});
                            });
                        } else {
                            //remove old result
                            Q_ASSERT(!oldSelectionResult.isEmpty());
                            readEntity(oldSelectionResult, [&](const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Operation) {
                                callback({entity, Sink::Operation_Removal});
                            });

                            //If the last item has been removed, then there's nothing to add
                            if (!selectionResult.selection.isEmpty()) {
                                //add new result
                                mSelectedValues.insert(reductionValueBa, selectionResult.selection);
                                Q_ASSERT(!selectionResult.selection.isEmpty());
                                readEntity(selectionResult.selection, [&](const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Operation) {
                                    callback({entity, Sink::Operation_Creation, selectionResult.aggregateValues, selectionResult.aggregateIds});
                                });
                            }
                        }
                    }
                }
            }))
        {}
        return foundValue;
    }
};

class Bloom : public Filter {
public:
    typedef QSharedPointer<Bloom> Ptr;

    QByteArray mBloomProperty;

    Bloom(const QByteArray &bloomProperty, FilterBase::Ptr source, DataStoreQuery *store)
        : Filter(source, store),
        mBloomProperty(bloomProperty)
    {

    }

    virtual ~Bloom(){}

    bool next(const std::function<void(const ResultSet::Result &result)> &callback) Q_DECL_OVERRIDE {
        if (!mBloomed) {
            //Initially we bloom on the first value that matches.
            //From there on we just filter.
            bool foundValue = false;
            while(!foundValue && mSource->next([this, callback, &foundValue](const ResultSet::Result &result) {
                    mBloomValue = result.entity.getProperty(mBloomProperty);
                    auto results = indexLookup(mBloomProperty, mBloomValue);
                    for (const auto &r : results) {
                        readEntity(r, [&, this](const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Operation operation) {
                            callback({entity, Sink::Operation_Creation});
                            SinkTraceCtx(mDatastore->mLogCtx) << "Bloom result: " << entity.identifier() << operationName(operation);
                            foundValue = true;
                        });
                    }
                    return false;
                }))
            {}
            mBloomed = true;
            propertyFilter.insert(mBloomProperty, mBloomValue);
            return foundValue;
        } else {
            //Filter on bloom value
            return Filter::next(callback);
        }
    }
    QVariant mBloomValue;
    bool mBloomed = false;
};

DataStoreQuery::DataStoreQuery(const Sink::QueryBase &query, const QByteArray &type, EntityStore &store)
    : mType(type), mStore(store), mLogCtx(store.logContext().subContext("datastorequery"))
{
    //This is what we use during a new query
    setupQuery(query);
}

DataStoreQuery::DataStoreQuery(const DataStoreQuery::State &state, const QByteArray &type, Sink::Storage::EntityStore &store, bool incremental)
    : mType(type), mStore(store), mLogCtx(store.logContext().subContext("datastorequery"))
{
    //This is what we use when fetching more data, without having a new revision with incremental=false
    //And this is what we use when the data changed and we want to update with incremental = true
    mCollector = state.mCollector;
    mSource = state.mSource;

    auto source = mCollector;
    while (source) {
        source->mDatastore = this;
        source->mIncremental = incremental;
        source = source->mSource;
    }
}

DataStoreQuery::~DataStoreQuery()
{

}

DataStoreQuery::State::Ptr DataStoreQuery::getState()
{
    auto state = State::Ptr::create();
    state->mSource = mSource;
    state->mCollector = mCollector;
    return state;
}

void DataStoreQuery::readEntity(const QByteArray &key, const BufferCallback &resultCallback)
{
    mStore.readLatest(mType, key, resultCallback);
}

void DataStoreQuery::readPrevious(const QByteArray &key, const std::function<void (const ApplicationDomain::ApplicationDomainType &)> &callback)
{
    mStore.readPrevious(mType, key, mStore.maxRevision(), callback);
}

QVector<QByteArray> DataStoreQuery::indexLookup(const QByteArray &property, const QVariant &value)
{
    return mStore.indexLookup(mType, property, value);
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

QByteArrayList DataStoreQuery::executeSubquery(const QueryBase &subquery)
{
    Q_ASSERT(!subquery.type().isEmpty());
    auto sub = DataStoreQuery(subquery, subquery.type(), mStore);
    auto result = sub.execute();
    QByteArrayList ids;
    while (result.next([&ids](const ResultSet::Result &result) {
            ids << result.entity.identifier();
        }))
    {}
    return ids;
}

void DataStoreQuery::setupQuery(const Sink::QueryBase &query_)
{
    auto query = query_;
    auto baseFilters = query.getBaseFilters();
    //Resolve any subqueries we have
    for (const auto &k : baseFilters.keys()) {
        const auto comparator = baseFilters.value(k);
        if (comparator.value.canConvert<Query>()) {
            SinkTraceCtx(mLogCtx) << "Executing subquery for property: " << k;
            const auto result = executeSubquery(comparator.value.value<Query>());
            baseFilters.insert(k, Query::Comparator(QVariant::fromValue(result), Query::Comparator::In));
        }
    }
    query.setBaseFilters(baseFilters);

    QByteArray appliedSorting;

    //Determine initial set
    mSource = [&]() {
        if (!query.ids().isEmpty()) {
            //We have a set of ids as a starting point
            return Source::Ptr::create(query.ids().toVector(), this);
        } else {
            QSet<QByteArray> appliedFilters;
            auto resultSet = mStore.indexLookup(mType, query, appliedFilters, appliedSorting);
            if (!appliedFilters.isEmpty()) {
                //We have an index lookup as starting point
                return Source::Ptr::create(resultSet, this);
            }
            // We do a full scan if there were no indexes available to create the initial set (this is going to be expensive for large sets).
            return Source::Ptr::create(mStore.fullScan(mType), this);
        }
    }();

    FilterBase::Ptr baseSet = mSource;
    if (!query.getBaseFilters().isEmpty()) {
        auto filter = Filter::Ptr::create(baseSet, this);
        //For incremental queries the remaining filters are not sufficient
        for (const auto &f : query.getBaseFilters().keys()) {
            filter->propertyFilter.insert(f, query.getFilter(f));
        }
        baseSet = filter;
    }
    /* if (appliedSorting.isEmpty() && !query.sortProperty.isEmpty()) { */
    /*     //Apply manual sorting */
    /*     baseSet = Sort::Ptr::create(baseSet, query.sortProperty); */
    /* } */

    //Setup the rest of the filter stages on top of the base set
    for (const auto &stage : query.getFilterStages()) {
        if (auto filter = stage.dynamicCast<Query::Filter>()) {
            auto f = Filter::Ptr::create(baseSet, this);
            f->propertyFilter = filter->propertyFilter;
            baseSet = f;
        } else if (auto filter = stage.dynamicCast<Query::Reduce>()) {
            auto reduction = Reduce::Ptr::create(filter->property, filter->selector.property, filter->selector.comparator, baseSet, this);
            for (const auto &aggregator : filter->aggregators) {
                reduction->mAggregators << Reduce::Aggregator(aggregator.operation, aggregator.propertyToCollect, aggregator.resultProperty);
            }
            reduction->propertyFilter = query.getBaseFilters();
            baseSet = reduction;
        } else if (auto filter = stage.dynamicCast<Query::Bloom>()) {
            baseSet = Bloom::Ptr::create(filter->property, baseSet, this);
        }
    }

    mCollector = Collector::Ptr::create(baseSet, this);
}

QVector<QByteArray> DataStoreQuery::loadIncrementalResultSet(qint64 baseRevision)
{
    QVector<QByteArray> changedKeys;
    mStore.readRevisions(baseRevision, mType, [&](const QByteArray &key) {
        changedKeys << key;
    });
    return changedKeys;
}

ResultSet DataStoreQuery::update(qint64 baseRevision)
{
    SinkTraceCtx(mLogCtx) << "Executing query update from revision " << baseRevision;
    auto incrementalResultSet = loadIncrementalResultSet(baseRevision);
    SinkTraceCtx(mLogCtx) << "Incremental changes: " << incrementalResultSet;
    mSource->add(incrementalResultSet);
    ResultSet::ValueGenerator generator = [this](const ResultSet::Callback &callback) -> bool {
        if (mCollector->next([this, callback](const ResultSet::Result &result) {
                SinkTraceCtx(mLogCtx) << "Got incremental result: " << result.entity.identifier() << operationName(result.operation);
                callback(result);
            }))
        {
            return true;
        }
        return false;
    };
    return ResultSet(generator, [this]() { mCollector->skip(); });
}

void DataStoreQuery::updateComplete()
{
    mSource->mIncrementalIds.clear();
    auto source = mCollector;
    while (source) {
        source->updateComplete();
        source = source->mSource;
    }
}

ResultSet DataStoreQuery::execute()
{
    SinkTraceCtx(mLogCtx) << "Executing query";

    Q_ASSERT(mCollector);
    ResultSet::ValueGenerator generator = [this](const ResultSet::Callback &callback) -> bool {
        if (mCollector->next([this, callback](const ResultSet::Result &result) {
                if (result.operation != Sink::Operation_Removal) {
                    SinkTraceCtx(mLogCtx) << "Got initial result: " << result.entity.identifier() << result.operation;
                    callback(ResultSet::Result{result.entity, Sink::Operation_Creation, result.aggregateValues, result.aggregateIds});
                }
            }))
        {
            return true;
        }
        return false;
    };
    return ResultSet(generator, [this]() { mCollector->skip(); });
}
