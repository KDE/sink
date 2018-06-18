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

#include <cmath>

using namespace Sink;

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
    static unsigned int uint_num_digits = (unsigned int)std::log10(std::numeric_limits<unsigned int>::max()) + 1;
    return QByteArray::number(std::numeric_limits<unsigned int>::max() - date.toTime_t()).rightJustified(uint_num_digits, '0');
}

static QByteArray toSortableByteArray(const QVariant &value)
{
    if (!value.isValid()) {
        // FIXME: we don't know the type, so we don't know what to return
        // This mean we're fixing every sorted index keys to use unsigned int
        return QByteArray::number(std::numeric_limits<unsigned int>::max());
    }

    switch (value.type()) {
        case QMetaType::QDateTime:
            return toSortableByteArrayImpl(value.toDateTime());
        default:
            SinkWarning() << "Not knowing how to convert a" << value.typeName()
                          << "to a sortable key, falling back to default conversion";
            return getByteArray(value);
    }
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

template <>
void TypeIndex::addProperty<QByteArray>(const QByteArray &property)
{
    auto indexer = [this, property](bool add, const QByteArray &identifier, const QVariant &value, Sink::Storage::DataStore::Transaction &transaction) {
        // SinkTraceCtx(mLogCtx) << "Indexing " << mType + ".index." + property << value.toByteArray();
        if (add) {
            Index(indexName(property), transaction).add(getByteArray(value), identifier);
        } else {
            Index(indexName(property), transaction).remove(getByteArray(value), identifier);
        }
    };
    mIndexer.insert(property, indexer);
    mProperties << property;
}

template <>
void TypeIndex::addProperty<bool>(const QByteArray &property)
{
    auto indexer = [this, property](bool add, const QByteArray &identifier, const QVariant &value, Sink::Storage::DataStore::Transaction &transaction) {
        if (add) {
            Index(indexName(property), transaction).add(getByteArray(value), identifier);
        } else {
            Index(indexName(property), transaction).remove(getByteArray(value), identifier);
        }
    };
    mIndexer.insert(property, indexer);
    mProperties << property;
}

template <>
void TypeIndex::addProperty<QString>(const QByteArray &property)
{
    auto indexer = [this, property](bool add, const QByteArray &identifier, const QVariant &value, Sink::Storage::DataStore::Transaction &transaction) {
        // SinkTraceCtx(mLogCtx) << "Indexing " << mType + ".index." + property << value.toByteArray();
        if (add) {
            Index(indexName(property), transaction).add(getByteArray(value), identifier);
        } else {
            Index(indexName(property), transaction).remove(getByteArray(value), identifier);
        }
    };
    mIndexer.insert(property, indexer);
    mProperties << property;
}

template <>
void TypeIndex::addProperty<QDateTime>(const QByteArray &property)
{
    auto indexer = [this, property](bool add, const QByteArray &identifier, const QVariant &value, Sink::Storage::DataStore::Transaction &transaction) {
        //SinkTraceCtx(mLogCtx) << "Indexing " << mType + ".index." + property << getByteArray(value);
        if (add) {
            Index(indexName(property), transaction).add(getByteArray(value), identifier);
        } else {
            Index(indexName(property), transaction).remove(getByteArray(value), identifier);
        }
    };
    mIndexer.insert(property, indexer);
    mProperties << property;
}

template <>
void TypeIndex::addProperty<ApplicationDomain::Reference>(const QByteArray &property)
{
    addProperty<QByteArray>(property);
}

template <>
void TypeIndex::addSortedProperty<QDateTime>(const QByteArray &property)
{
    auto indexer = [this, property](bool add, const QByteArray &identifier, const QVariant &value,
                       Sink::Storage::DataStore::Transaction &transaction) {
        const auto sortableDate = toSortableByteArray(value);
        if (add) {
            Index(sortedIndexName(property), transaction).add(sortableDate, identifier);
        } else {
            Index(sortedIndexName(property), transaction).remove(sortableDate, identifier);
        }
    };
    mSortIndexer.insert(property, indexer);
    mSortedProperties << property;
}

template <>
void TypeIndex::addPropertyWithSorting<QByteArray, QDateTime>(const QByteArray &property, const QByteArray &sortProperty)
{
    auto indexer = [=](bool add, const QByteArray &identifier, const QVariant &value, const QVariant &sortValue, Sink::Storage::DataStore::Transaction &transaction) {
        const auto date = sortValue.toDateTime();
        const auto propertyValue = getByteArray(value);
        if (add) {
            Index(indexName(property, sortProperty), transaction).add(propertyValue + toSortableByteArray(date), identifier);
        } else {
            Index(indexName(property, sortProperty), transaction).remove(propertyValue + toSortableByteArray(date), identifier);
        }
    };
    mGroupedSortIndexer.insert(property + sortProperty, indexer);
    mGroupedSortedProperties.insert(property, sortProperty);
}

template <>
void TypeIndex::addPropertyWithSorting<ApplicationDomain::Reference, QDateTime>(const QByteArray &property, const QByteArray &sortProperty)
{
    addPropertyWithSorting<QByteArray, QDateTime>(property, sortProperty);
}

void TypeIndex::updateIndex(bool add, const QByteArray &identifier, const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId)
{
    for (const auto &property : mProperties) {
        const auto value = entity.getProperty(property);
        auto indexer = mIndexer.value(property);
        indexer(add, identifier, value, transaction);
    }
    for (const auto &property : mSortedProperties) {
        const auto value = entity.getProperty(property);
        auto indexer = mSortIndexer.value(property);
        indexer(add, identifier, value, transaction);
    }
    for (auto it = mGroupedSortedProperties.constBegin(); it != mGroupedSortedProperties.constEnd(); it++) {
        const auto value = entity.getProperty(it.key());
        const auto sortValue = entity.getProperty(it.value());
        auto indexer = mGroupedSortIndexer.value(it.key() + it.value());
        indexer(add, identifier, value, sortValue, transaction);
    }
    for (const auto &indexer : mCustomIndexer) {
        indexer->setup(this, &transaction, resourceInstanceId);
        if (add) {
            indexer->add(entity);
        } else {
            indexer->remove(entity);
        }
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

void TypeIndex::add(const QByteArray &identifier, const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId)
{
    updateIndex(true, identifier, entity, transaction, resourceInstanceId);
}

void TypeIndex::remove(const QByteArray &identifier, const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId)
{
    updateIndex(false, identifier, entity, transaction, resourceInstanceId);
}

static QVector<QByteArray> indexLookup(Index &index, QueryBase::Comparator filter,
    std::function<QByteArray(const QVariant &)> valueToKey = getByteArray)
{
    QVector<QByteArray> keys;
    QByteArrayList lookupKeys;
    if (filter.comparator == Query::Comparator::Equals) {
        lookupKeys << valueToKey(filter.value);
    } else if (filter.comparator == Query::Comparator::In) {
        for(const QVariant &value : filter.value.value<QVariantList>()) {
            lookupKeys << valueToKey(value);
        }
    } else {
        Q_ASSERT(false);
    }

    for (const auto &lookupKey : lookupKeys) {
        index.lookup(lookupKey, [&](const QByteArray &value) { keys << value; },
            [lookupKey](const Index::Error &error) {
                SinkWarning() << "Lookup error in index: " << error.message << lookupKey;
            },
            true);
    }
    return keys;
}

static QVector<QByteArray> sortedIndexLookup(Index &index, QueryBase::Comparator filter)
{
    if (filter.comparator == Query::Comparator::In || filter.comparator == Query::Comparator::Contains) {
        SinkWarning() << "In and Contains comparison not supported on sorted indexes";
    }

    if (filter.comparator != Query::Comparator::Within) {
        return indexLookup(index, filter, toSortableByteArray);
    }

    QVector<QByteArray> keys;

    QByteArray lowerBound, upperBound;
    auto bounds = filter.value.value<QVariantList>();
    if (bounds[0].canConvert<QDateTime>()) {
        // Inverse the bounds because dates are stored newest first
        upperBound = toSortableByteArray(bounds[0].toDateTime());
        lowerBound = toSortableByteArray(bounds[1].toDateTime());
    } else {
        lowerBound = bounds[0].toByteArray();
        upperBound = bounds[1].toByteArray();
    }

    index.rangeLookup(lowerBound, upperBound, [&](const QByteArray &value) { keys << value; },
        [bounds](const Index::Error &error) {
            SinkWarning() << "Lookup error in index:" << error.message
                          << "with bounds:" << bounds[0] << bounds[1];
        });

    return keys;
}

QVector<QByteArray> TypeIndex::query(const Sink::QueryBase &query, QSet<QByteArray> &appliedFilters, QByteArray &appliedSorting, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId)
{
    const auto baseFilters = query.getBaseFilters();
    for (auto it = baseFilters.constBegin(); it != baseFilters.constEnd(); it++) {
        if (it.value().comparator == QueryBase::Comparator::Fulltext) {
            FulltextIndex fulltextIndex{resourceInstanceId};
            const auto keys = fulltextIndex.lookup(it.value().value.toString());
            appliedFilters << it.key();
            SinkTraceCtx(mLogCtx) << "Fulltext index lookup found " << keys.size() << " keys.";
            return keys;
        }
    }

    for (auto it = mGroupedSortedProperties.constBegin(); it != mGroupedSortedProperties.constEnd(); it++) {
        if (query.hasFilter(it.key()) && query.sortProperty() == it.value()) {
            Index index(indexName(it.key(), it.value()), transaction);
            const auto keys = indexLookup(index, query.getFilter(it.key()));
            appliedFilters << it.key();
            appliedSorting = it.value();
            SinkTraceCtx(mLogCtx) << "Grouped sorted index lookup on " << it.key() << it.value() << " found " << keys.size() << " keys.";
            return keys;
        }
    }

    for (const auto &property : mSortedProperties) {
        if (query.hasFilter(property)) {
            Index index(sortedIndexName(property), transaction);
            const auto keys = sortedIndexLookup(index, query.getFilter(property));
            appliedFilters << property;
            SinkTraceCtx(mLogCtx) << "Sorted index lookup on " << property << " found " << keys.size() << " keys.";
            return keys;
        }
    }

    for (const auto &property : mProperties) {
        if (query.hasFilter(property)) {
            Index index(indexName(property), transaction);
            const auto keys = indexLookup(index, query.getFilter(property));
            appliedFilters << property;
            SinkTraceCtx(mLogCtx) << "Index lookup on " << property << " found " << keys.size() << " keys.";
            return keys;
        }
    }
    SinkTraceCtx(mLogCtx) << "No matching index";
    return {};
}

QVector<QByteArray> TypeIndex::lookup(const QByteArray &property, const QVariant &value, Sink::Storage::DataStore::Transaction &transaction)
{
    SinkTraceCtx(mLogCtx) << "Index lookup on property: " << property << mSecondaryProperties.keys() << mProperties;
    if (mProperties.contains(property)) {
        QVector<QByteArray> keys;
        Index index(indexName(property), transaction);
        const auto lookupKey = getByteArray(value);
        index.lookup(
            lookupKey, [&](const QByteArray &value) { keys << value; }, [property](const Index::Error &error) { SinkWarning() << "Error in index: " << error.message << property; });
        SinkTraceCtx(mLogCtx) << "Index lookup on " << property << " found " << keys.size() << " keys.";
        return keys;
    } else if (mSecondaryProperties.contains(property)) {
        //Lookups on secondary indexes first lookup the key, and then lookup the results again to resolve to entity id's
        QVector<QByteArray> keys;
        auto resultProperty = mSecondaryProperties.value(property);

        QVector<QByteArray> secondaryKeys;
        Index index(indexName(property + resultProperty), transaction);
        const auto lookupKey = getByteArray(value);
        index.lookup(
            lookupKey, [&](const QByteArray &value) { secondaryKeys << value; }, [property](const Index::Error &error) { SinkWarning() << "Error in index: " << error.message << property; });
        SinkTraceCtx(mLogCtx) << "Looked up secondary keys for the following lookup key: " << lookupKey << " => " << secondaryKeys;
        for (const auto &secondary : secondaryKeys) {
            keys += lookup(resultProperty, secondary, transaction);
        }
        return keys;
    } else {
        SinkWarning() << "Tried to lookup " << property << " but couldn't find value";
    }
    return QVector<QByteArray>();
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
    QVector<QByteArray> keys;
    Index index(indexName(leftName + rightName), *mTransaction);
    const auto lookupKey = getByteArray(value);
    index.lookup(
        lookupKey, [&](const QByteArray &value) { keys << QByteArray{value.constData(), value.size()}; }, [=](const Index::Error &error) { SinkWarning() << "Lookup error in secondary index: " << error.message << value << lookupKey; });

    return keys;
}
