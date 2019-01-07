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
#include "typeindex.h"

#include "log.h"
#include "index.h"
#include "fulltextindex.h"

#include <QDateTime>
#include <QDataStream>

using namespace Sink;

using Storage::Identifier;

static QByteArray getByteArray(const QVariant &value)
{
    if (value.type() == QVariant::DateTime) {
        QByteArray result;
        QDataStream ds(&result, QIODevice::WriteOnly);
        ds << value.toDateTime();
        return result;
    }
    if (value.type() == QVariant::Bool) {
        return value.toBool() ? "t" : "f";
    }
    if (value.canConvert<Sink::ApplicationDomain::Reference>()) {
        const auto ba = value.value<Sink::ApplicationDomain::Reference>().value;
        if (!ba.isEmpty()) {
            return ba;
        }
    }
    if (value.isValid() && !value.toByteArray().isEmpty()) {
        return value.toByteArray();
    }
    // LMDB can't handle empty keys, so use something different
    return "toplevel";
}

static QByteArray toSortableByteArrayImpl(const QDateTime &date)
{
    // Sort invalid last
    if (!date.isValid()) {
        return QByteArray::number(std::numeric_limits<unsigned int>::max());
    }
    return padNumber(std::numeric_limits<unsigned int>::max() - date.toTime_t());
}

static QByteArray toSortableByteArray(const QVariant &value)
{
    if (!value.isValid()) {
        // FIXME: we don't know the type, so we don't know what to return
        // This mean we're fixing every sorted index keys to use unsigned int
        return QByteArray::number(std::numeric_limits<unsigned int>::max());
    }

    if (value.canConvert<QDateTime>()) {
        return toSortableByteArrayImpl(value.toDateTime());
    }
    SinkWarning() << "Not knowing how to convert a" << value.typeName()
                    << "to a sortable key, falling back to default conversion";
    return getByteArray(value);
}

TypeIndex::TypeIndex(const QByteArray &type, const Sink::Log::Context &ctx) : mLogCtx(ctx), mType(type)
{
}

QByteArray TypeIndex::indexName(const QByteArray &property, const QByteArray &sortProperty) const
{
    if (sortProperty.isEmpty()) {
        return mType + ".index." + property;
    }
    return mType + ".index." + property + ".sort." + sortProperty;
}

QByteArray TypeIndex::sortedIndexName(const QByteArray &property) const
{
    return mType + ".index." + property + ".sorted";
}

QByteArray TypeIndex::sampledPeriodIndexName(const QByteArray &rangeBeginProperty, const QByteArray &rangeEndProperty) const
{
    return mType + ".index." + rangeBeginProperty + ".range." + rangeEndProperty;
}

static unsigned int bucketOf(const QVariant &value)
{
    if (value.canConvert<QDateTime>()) {
        return value.value<QDateTime>().date().toJulianDay() / 7;
    }
    SinkError() << "Not knowing how to get the bucket of a" << value.typeName();
    return {};
}

static void update(TypeIndex::Action action, const QByteArray &indexName, const QByteArray &key, const QByteArray &value, Sink::Storage::DataStore::Transaction &transaction)
{
    Index index(indexName, transaction);
    switch (action) {
        case TypeIndex::Add:
            index.add(key, value);
            break;
        case TypeIndex::Remove:
            index.remove(key, value);
            break;
    }
}

void TypeIndex::addProperty(const QByteArray &property)
{
    auto indexer = [=](Action action, const Identifier &identifier, const QVariant &value, Sink::Storage::DataStore::Transaction &transaction) {
        update(action, indexName(property), getByteArray(value), identifier.toInternalByteArray(), transaction);
    };
    mIndexer.insert(property, indexer);
    mProperties << property;
}

template <>
void TypeIndex::addSortedProperty<QDateTime>(const QByteArray &property)
{
    auto indexer = [=](Action action, const Identifier &identifier, const QVariant &value,
                       Sink::Storage::DataStore::Transaction &transaction) {
        update(action, sortedIndexName(property), toSortableByteArray(value), identifier.toInternalByteArray(), transaction);
    };
    mSortIndexer.insert(property, indexer);
    mSortedProperties << property;
}

template <>
void TypeIndex::addPropertyWithSorting<QByteArray, QDateTime>(const QByteArray &property, const QByteArray &sortProperty)
{
    auto indexer = [=](Action action, const Identifier &identifier, const QVariant &value, const QVariant &sortValue, Sink::Storage::DataStore::Transaction &transaction) {
        const auto date = sortValue.toDateTime();
        const auto propertyValue = getByteArray(value);
        update(action, indexName(property, sortProperty), propertyValue + toSortableByteArray(date), identifier.toInternalByteArray(), transaction);
    };
    mGroupedSortIndexer.insert(property + sortProperty, indexer);
    mGroupedSortedProperties.insert(property, sortProperty);
}

template <>
void TypeIndex::addPropertyWithSorting<ApplicationDomain::Reference, QDateTime>(const QByteArray &property, const QByteArray &sortProperty)
{
    addPropertyWithSorting<QByteArray, QDateTime>(property, sortProperty);
}

template <>
void TypeIndex::addSampledPeriodIndex<QDateTime, QDateTime>(
    const QByteArray &beginProperty, const QByteArray &endProperty)
{
    auto indexer = [=](Action action, const Identifier &identifier, const QVariant &begin,
                       const QVariant &end, Sink::Storage::DataStore::Transaction &transaction) {
        const auto beginDate = begin.toDateTime();
        const auto endDate = end.toDateTime();

        auto beginBucket = bucketOf(beginDate);
        auto endBucket   = bucketOf(endDate);

        if (beginBucket > endBucket) {
            SinkError() << "End bucket greater than begin bucket";
            return;
        }

        Index index(sampledPeriodIndexName(beginProperty, endProperty), transaction);
        for (auto bucket = beginBucket; bucket <= endBucket; ++bucket) {
            QByteArray bucketKey = padNumber(bucket);
            switch (action) {
                case TypeIndex::Add:
                    index.add(bucketKey, identifier.toInternalByteArray());
                    break;
                case TypeIndex::Remove:
                    index.remove(bucketKey, identifier.toInternalByteArray(), true);
                    break;
            }
        }
    };

    mSampledPeriodProperties.insert({ beginProperty, endProperty });
    mSampledPeriodIndexer.insert({ beginProperty, endProperty }, indexer);
}

void TypeIndex::updateIndex(Action action, const Identifier &identifier, const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId)
{
    for (const auto &property : mProperties) {
        const auto value = entity.getProperty(property);
        auto indexer = mIndexer.value(property);
        indexer(action, identifier, value, transaction);
    }
    for (const auto &properties : mSampledPeriodProperties) {
        auto indexer = mSampledPeriodIndexer.value(properties);
        auto indexRanges = entity.getProperty("indexRanges");
        if (indexRanges.isValid()) {
            //This is to override the indexed ranges from the evenpreprocessor
            const auto list = indexRanges.value<QList<QPair<QDateTime, QDateTime>>>();
            for (const auto &period : list) {
                indexer(action, identifier, period.first, period.second, transaction);
            }
        } else {
            //This is the regular case
            //NOTE Since we don't generate the ranges for removal we just end up trying to remove all possible buckets here instead.
            const auto beginValue = entity.getProperty(properties.first);
            const auto endValue   = entity.getProperty(properties.second);
            indexer(action, identifier, beginValue, endValue, transaction);
        }
    }
    for (const auto &property : mSortedProperties) {
        const auto value = entity.getProperty(property);
        auto indexer = mSortIndexer.value(property);
        indexer(action, identifier, value, transaction);
    }
    for (auto it = mGroupedSortedProperties.constBegin(); it != mGroupedSortedProperties.constEnd(); it++) {
        const auto value = entity.getProperty(it.key());
        const auto sortValue = entity.getProperty(it.value());
        auto indexer = mGroupedSortIndexer.value(it.key() + it.value());
        indexer(action, identifier, value, sortValue, transaction);
    }

}

void TypeIndex::commitTransaction()
{
    for (const auto &indexer : mCustomIndexer) {
        indexer->commitTransaction();
    }
}

void TypeIndex::abortTransaction()
{
    for (const auto &indexer : mCustomIndexer) {
        indexer->abortTransaction();
    }
}

void TypeIndex::add(const Identifier &identifier, const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId)
{
    updateIndex(Add, identifier, entity, transaction, resourceInstanceId);
    for (const auto &indexer : mCustomIndexer) {
        indexer->setup(this, &transaction, resourceInstanceId);
        indexer->add(entity);
    }
}

void TypeIndex::modify(const Identifier &identifier, const Sink::ApplicationDomain::ApplicationDomainType &oldEntity, const Sink::ApplicationDomain::ApplicationDomainType &newEntity, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId)
{
    updateIndex(Remove, identifier, oldEntity, transaction, resourceInstanceId);
    updateIndex(Add, identifier, newEntity, transaction, resourceInstanceId);
    for (const auto &indexer : mCustomIndexer) {
        indexer->setup(this, &transaction, resourceInstanceId);
        indexer->modify(oldEntity, newEntity);
    }
}

void TypeIndex::remove(const Identifier &identifier, const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId)
{
    updateIndex(Remove, identifier, entity, transaction, resourceInstanceId);
    for (const auto &indexer : mCustomIndexer) {
        indexer->setup(this, &transaction, resourceInstanceId);
        indexer->remove(entity);
    }
}

static QVector<Identifier> indexLookup(Index &index, QueryBase::Comparator filter,
    std::function<QByteArray(const QVariant &)> valueToKey = getByteArray)
{
    QVector<Identifier> keys;
    QByteArrayList lookupKeys;
    if (filter.comparator == Query::Comparator::Equals) {
        lookupKeys << valueToKey(filter.value);
    } else if (filter.comparator == Query::Comparator::In) {
        for (const QVariant &value : filter.value.value<QVariantList>()) {
            lookupKeys << valueToKey(value);
        }
    } else {
        Q_ASSERT(false);
    }

    for (const auto &lookupKey : lookupKeys) {
        index.lookup(lookupKey,
            [&](const QByteArray &value) {
                keys << Identifier::fromInternalByteArray(value);
            },
            [lookupKey](const Index::Error &error) {
                SinkWarning() << "Lookup error in index: " << error.message << lookupKey;
            },
            true);
    }
    return keys;
}

static QVector<Identifier> sortedIndexLookup(Index &index, QueryBase::Comparator filter)
{
    if (filter.comparator == Query::Comparator::In || filter.comparator == Query::Comparator::Contains) {
        SinkWarning() << "In and Contains comparison not supported on sorted indexes";
    }

    if (filter.comparator != Query::Comparator::Within) {
        return indexLookup(index, filter, toSortableByteArray);
    }

    QByteArray lowerBound, upperBound;
    const auto bounds = filter.value.value<QVariantList>();
    if (bounds[0].canConvert<QDateTime>()) {
        // Inverse the bounds because dates are stored newest first
        upperBound = toSortableByteArray(bounds[0].toDateTime());
        lowerBound = toSortableByteArray(bounds[1].toDateTime());
    } else {
        lowerBound = bounds[0].toByteArray();
        upperBound = bounds[1].toByteArray();
    }

    QVector<Identifier> keys;
    index.rangeLookup(lowerBound, upperBound,
        [&](const QByteArray &value) {
            const auto id = Identifier::fromInternalByteArray(value);
            //Deduplicate because an id could be in multiple buckets
            if (!keys.contains(id)) {
                keys << id;
            }
        },
        [&](const Index::Error &error) {
            SinkWarning() << "Lookup error in index:" << error.message
                          << "with bounds:" << bounds[0] << bounds[1];
        });

    return keys;
}

static QVector<Identifier> sampledIndexLookup(Index &index, QueryBase::Comparator filter)
{
    if (filter.comparator != Query::Comparator::Overlap) {
        SinkWarning() << "Comparisons other than Overlap not supported on sampled period indexes";
        return {};
    }


    const auto bounds = filter.value.value<QVariantList>();

    const auto lowerBucket = padNumber(bucketOf(bounds[0]));
    const auto upperBucket = padNumber(bucketOf(bounds[1]));

    SinkTrace() << "Looking up from bucket:" << lowerBucket << "to:" << upperBucket;

    QVector<Identifier> keys;
    index.rangeLookup(lowerBucket, upperBucket,
        [&](const QByteArray &value) {
            const auto id = Identifier::fromInternalByteArray(value);
            //Deduplicate because an id could be in multiple buckets
            if (!keys.contains(id)) {
                keys << id;
            }
        },
        [&](const Index::Error &error) {
            SinkWarning() << "Lookup error in index:" << error.message
                          << "with bounds:" << bounds[0] << bounds[1];
        });

    return keys;
}

QVector<Identifier> TypeIndex::query(const Sink::QueryBase &query, QSet<QByteArrayList> &appliedFilters, QByteArray &appliedSorting, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId)
{
    const auto baseFilters = query.getBaseFilters();
    for (auto it = baseFilters.constBegin(); it != baseFilters.constEnd(); it++) {
        if (it.value().comparator == QueryBase::Comparator::Fulltext) {
            FulltextIndex fulltextIndex{resourceInstanceId};
            QVector<Identifier> keys;
            const auto ids = fulltextIndex.lookup(it.value().value.toString());
            keys.reserve(ids.size());
            for (const auto &id : ids) {
                keys.append(Identifier::fromDisplayByteArray(id));
            }
            appliedFilters << it.key();
            SinkTraceCtx(mLogCtx) << "Fulltext index lookup found " << keys.size() << " keys.";
            return keys;
        }
    }

    for (auto it = baseFilters.constBegin(); it != baseFilters.constEnd(); it++) {
        if (it.value().comparator == QueryBase::Comparator::Overlap) {
            if (mSampledPeriodProperties.contains({it.key()[0], it.key()[1]})) {
                Index index(sampledPeriodIndexName(it.key()[0], it.key()[1]), transaction);
                const auto keys = sampledIndexLookup(index, query.getFilter(it.key()));
                appliedFilters << it.key();
                SinkTraceCtx(mLogCtx) << "Sampled period index lookup on" << it.key() << "found" << keys.size() << "keys.";
                return keys;
            } else {
                SinkWarning() << "Overlap search without sampled period index";
            }
        }
    }

    for (auto it = mGroupedSortedProperties.constBegin(); it != mGroupedSortedProperties.constEnd(); it++) {
        if (query.hasFilter(it.key()) && query.sortProperty() == it.value()) {
            Index index(indexName(it.key(), it.value()), transaction);
            const auto keys = indexLookup(index, query.getFilter(it.key()));
            appliedFilters.insert({it.key()});
            appliedSorting = it.value();
            SinkTraceCtx(mLogCtx) << "Grouped sorted index lookup on " << it.key() << it.value() << " found " << keys.size() << " keys.";
            return keys;
        }
    }

    for (const auto &property : mSortedProperties) {
        if (query.hasFilter(property)) {
            Index index(sortedIndexName(property), transaction);
            const auto keys = sortedIndexLookup(index, query.getFilter(property));
            appliedFilters.insert({property});
            SinkTraceCtx(mLogCtx) << "Sorted index lookup on " << property << " found " << keys.size() << " keys.";
            return keys;
        }
    }

    for (const auto &property : mProperties) {
        if (query.hasFilter(property)) {
            Index index(indexName(property), transaction);
            const auto keys = indexLookup(index, query.getFilter(property));
            appliedFilters.insert({property});
            SinkTraceCtx(mLogCtx) << "Index lookup on " << property << " found " << keys.size() << " keys.";
            return keys;
        }
    }
    SinkTraceCtx(mLogCtx) << "No matching index";
    return {};
}

QVector<Identifier> TypeIndex::lookup(const QByteArray &property, const QVariant &value,
    Sink::Storage::DataStore::Transaction &transaction)
{
    SinkTraceCtx(mLogCtx) << "Index lookup on property: " << property << mSecondaryProperties.keys() << mProperties;
    if (mProperties.contains(property)) {
        QVector<Identifier> keys;
        Index index(indexName(property), transaction);
        const auto lookupKey = getByteArray(value);
        index.lookup(lookupKey,
            [&](const QByteArray &value) {
                keys << Identifier::fromInternalByteArray(value);
            },
            [property](const Index::Error &error) {
                SinkWarning() << "Error in index: " << error.message << property;
            });
        SinkTraceCtx(mLogCtx) << "Index lookup on " << property << " found " << keys.size() << " keys.";
        return keys;
    } else if (mSecondaryProperties.contains(property)) {
        // Lookups on secondary indexes first lookup the key, and then lookup the results again to
        // resolve to entity id's
        QVector<Identifier> keys;
        auto resultProperty = mSecondaryProperties.value(property);

        QVector<QByteArray> secondaryKeys;
        Index index(indexName(property + resultProperty), transaction);
        const auto lookupKey = getByteArray(value);
        index.lookup(lookupKey, [&](const QByteArray &value) { secondaryKeys << value; },
            [property](const Index::Error &error) {
                SinkWarning() << "Error in index: " << error.message << property;
            });
        SinkTraceCtx(mLogCtx) << "Looked up secondary keys for the following lookup key: " << lookupKey
                              << " => " << secondaryKeys;
        for (const auto &secondary : secondaryKeys) {
            keys += lookup(resultProperty, secondary, transaction);
        }
        return keys;
    } else {
        SinkWarning() << "Tried to lookup " << property << " but couldn't find value";
    }
    return {};
}

template <>
void TypeIndex::index<QByteArray, QByteArray>(const QByteArray &leftName, const QByteArray &rightName, const QVariant &leftValue, const QVariant &rightValue, Sink::Storage::DataStore::Transaction &transaction)
{
    Index(indexName(leftName + rightName), transaction).add(getByteArray(leftValue), getByteArray(rightValue));
}

template <>
void TypeIndex::index<QString, QByteArray>(const QByteArray &leftName, const QByteArray &rightName, const QVariant &leftValue, const QVariant &rightValue, Sink::Storage::DataStore::Transaction &transaction)
{
    Index(indexName(leftName + rightName), transaction).add(getByteArray(leftValue), getByteArray(rightValue));
}

template <>
void TypeIndex::unindex<QByteArray, QByteArray>(const QByteArray &leftName, const QByteArray &rightName, const QVariant &leftValue, const QVariant &rightValue, Sink::Storage::DataStore::Transaction &transaction)
{
    Index(indexName(leftName + rightName), transaction).remove(getByteArray(leftValue), getByteArray(rightValue));
}

template <>
void TypeIndex::unindex<QString, QByteArray>(const QByteArray &leftName, const QByteArray &rightName, const QVariant &leftValue, const QVariant &rightValue, Sink::Storage::DataStore::Transaction &transaction)
{
    Index(indexName(leftName + rightName), transaction).remove(getByteArray(leftValue), getByteArray(rightValue));
}

template <>
QVector<QByteArray> TypeIndex::secondaryLookup<QByteArray>(const QByteArray &leftName, const QByteArray &rightName, const QVariant &value)
{
    QVector<QByteArray> keys;
    Index index(indexName(leftName + rightName), *mTransaction);
    const auto lookupKey = getByteArray(value);
    index.lookup(
        lookupKey, [&](const QByteArray &value) { keys << QByteArray{value.constData(), value.size()}; }, [=](const Index::Error &error) { SinkWarning() << "Lookup error in secondary index: " << error.message << value << lookupKey; });

    return keys;
}

template <>
QVector<QByteArray> TypeIndex::secondaryLookup<QString>(const QByteArray &leftName, const QByteArray &rightName, const QVariant &value)
{
    return secondaryLookup<QByteArray>(leftName, rightName, value);
}
