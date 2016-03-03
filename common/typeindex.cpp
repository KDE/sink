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
#include <QDateTime>

#undef DEBUG_AREA
#define DEBUG_AREA "common.typeindex"

static QByteArray getByteArray(const QVariant &value)
{
    if (value.isValid() && !value.toByteArray().isEmpty()) {
        return value.toByteArray();
    }
    // LMDB can't handle empty keys, so use something different
    return "toplevel";
}

static QByteArray toSortableByteArray(const QDateTime &date)
{
    // Sort invalid last
    if (!date.isValid()) {
        return QByteArray::number(std::numeric_limits<unsigned int>::max());
    }
    return QByteArray::number(std::numeric_limits<unsigned int>::max() - date.toTime_t());
}


TypeIndex::TypeIndex(const QByteArray &type) : mType(type)
{
}

QByteArray TypeIndex::indexName(const QByteArray &property, const QByteArray &sortProperty) const
{
    if (sortProperty.isEmpty()) {
        return mType + ".index." + property;
    }
    return mType + ".index." + property + ".sort." + sortProperty;
}

template <>
void TypeIndex::addProperty<QByteArray>(const QByteArray &property)
{
    auto indexer = [this, property](const QByteArray &identifier, const QVariant &value, Sink::Storage::Transaction &transaction) {
        // Trace() << "Indexing " << mType + ".index." + property << value.toByteArray();
        Index(indexName(property), transaction).add(getByteArray(value), identifier);
    };
    mIndexer.insert(property, indexer);
    mProperties << property;
}

template <>
void TypeIndex::addProperty<QString>(const QByteArray &property)
{
    auto indexer = [this, property](const QByteArray &identifier, const QVariant &value, Sink::Storage::Transaction &transaction) {
        // Trace() << "Indexing " << mType + ".index." + property << value.toByteArray();
        Index(indexName(property), transaction).add(getByteArray(value), identifier);
    };
    mIndexer.insert(property, indexer);
    mProperties << property;
}

template <>
void TypeIndex::addProperty<QDateTime>(const QByteArray &property)
{
    auto indexer = [this, property](const QByteArray &identifier, const QVariant &value, Sink::Storage::Transaction &transaction) {
        const auto date = value.toDateTime();
        // Trace() << "Indexing " << mType + ".index." + property << date.toString();
        if (date.isValid()) {
            Index(indexName(property), transaction).add(date.toString().toLatin1(), identifier);
        }
    };
    mIndexer.insert(property, indexer);
    mProperties << property;
}

template <>
void TypeIndex::addPropertyWithSorting<QByteArray, QDateTime>(const QByteArray &property, const QByteArray &sortProperty)
{
    auto indexer = [=](const QByteArray &identifier, const QVariant &value, const QVariant &sortValue, Sink::Storage::Transaction &transaction) {
        const auto date = sortValue.toDateTime();
        const auto propertyValue = getByteArray(value);
        Index(indexName(property, sortProperty), transaction).add(propertyValue + toSortableByteArray(date), identifier);
    };
    mSortIndexer.insert(property + sortProperty, indexer);
    mSortedProperties.insert(property, sortProperty);
}

void TypeIndex::add(const QByteArray &identifier, const Sink::ApplicationDomain::BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction)
{
    for (const auto &property : mProperties) {
        const auto value = bufferAdaptor.getProperty(property);
        auto indexer = mIndexer.value(property);
        indexer(identifier, value, transaction);
    }
    for (auto it = mSortedProperties.constBegin(); it != mSortedProperties.constEnd(); it++) {
        const auto value = bufferAdaptor.getProperty(it.key());
        const auto sortValue = bufferAdaptor.getProperty(it.value());
        auto indexer = mSortIndexer.value(it.key() + it.value());
        indexer(identifier, value, sortValue, transaction);
    }
}

void TypeIndex::remove(const QByteArray &identifier, const Sink::ApplicationDomain::BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction)
{
    for (const auto &property : mProperties) {
        const auto value = bufferAdaptor.getProperty(property);
        // FIXME don't always convert to byte array
        Index(indexName(property), transaction).remove(getByteArray(value), identifier);
    }
    for (auto it = mSortedProperties.constBegin(); it != mSortedProperties.constEnd(); it++) {
        const auto propertyValue = bufferAdaptor.getProperty(it.key());
        const auto sortValue = bufferAdaptor.getProperty(it.value());
        if (sortValue.type() == QVariant::DateTime) {
            Index(indexName(it.key(), it.value()), transaction).remove(propertyValue.toByteArray() + toSortableByteArray(sortValue.toDateTime()), identifier);
        } else {
            Index(indexName(it.key(), it.value()), transaction).remove(propertyValue.toByteArray() + sortValue.toByteArray(), identifier);
        }
    }
}

ResultSet TypeIndex::query(const Sink::Query &query, QSet<QByteArray> &appliedFilters, QByteArray &appliedSorting, Sink::Storage::Transaction &transaction)
{
    QVector<QByteArray> keys;
    for (auto it = mSortedProperties.constBegin(); it != mSortedProperties.constEnd(); it++) {
        if (query.propertyFilter.contains(it.key()) && query.sortProperty == it.value()) {
            Index index(indexName(it.key(), it.value()), transaction);
            const auto lookupKey = getByteArray(query.propertyFilter.value(it.key()));
            Trace() << "looking for " << lookupKey;
            index.lookup(lookupKey, [&](const QByteArray &value) { keys << value; },
                [it](const Index::Error &error) { Warning() << "Error in index: " << error.message << it.key() << it.value(); }, true);
            appliedFilters << it.key();
            appliedSorting = it.value();
            Trace() << "Index lookup on " << it.key() << it.value() << " found " << keys.size() << " keys.";
            return ResultSet(keys);
        }
    }
    for (const auto &property : mProperties) {
        if (query.propertyFilter.contains(property)) {
            Index index(indexName(property), transaction);
            const auto lookupKey = getByteArray(query.propertyFilter.value(property));
            index.lookup(
                lookupKey, [&](const QByteArray &value) { keys << value; }, [property](const Index::Error &error) { Warning() << "Error in index: " << error.message << property; });
            appliedFilters << property;
            Trace() << "Index lookup on " << property << " found " << keys.size() << " keys.";
            return ResultSet(keys);
        }
    }
    Trace() << "No matching index";
    return ResultSet(keys);
}
