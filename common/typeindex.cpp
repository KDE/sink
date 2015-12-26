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

TypeIndex::TypeIndex(const QByteArray &type)
    : mType(type)
{

}

template<>
void TypeIndex::addProperty<QByteArray>(const QByteArray &property)
{
    auto indexer = [this, property](const QByteArray &identifier, const QVariant &value, Akonadi2::Storage::Transaction &transaction) {
        // Trace() << "Indexing " << mType + ".index." + property << value.toByteArray();
        if (value.isValid()) {
            Index(mType + ".index." + property, transaction).add(value.toByteArray(), identifier);
        } else {
            Index(mType + ".index." + property, transaction).add("toplevel", identifier);
        }
    };
    mIndexer.insert(property, indexer);
    mProperties << property;
}

template<>
void TypeIndex::addProperty<QString>(const QByteArray &property)
{
    auto indexer = [this, property](const QByteArray &identifier, const QVariant &value, Akonadi2::Storage::Transaction &transaction) {
        // Trace() << "Indexing " << mType + ".index." + property << value.toByteArray();
        if (value.isValid()) {
            Index(mType + ".index." + property, transaction).add(value.toByteArray(), identifier);
        } else {
            Index(mType + ".index." + property, transaction).add("toplevel", identifier);
        }
    };
    mIndexer.insert(property, indexer);
    mProperties << property;
}

template<>
void TypeIndex::addProperty<QDateTime>(const QByteArray &property)
{
    auto indexer = [this, property](const QByteArray &identifier, const QVariant &value, Akonadi2::Storage::Transaction &transaction) {
        // Trace() << "Indexing " << mType + ".index." + property << value.toByteArray();
        if (value.isValid()) {
            Index(mType + ".index." + property, transaction).add(value.toByteArray(), identifier);
        }
    };
    mIndexer.insert(property, indexer);
    mProperties << property;
}

void TypeIndex::add(const QByteArray &identifier, const Akonadi2::ApplicationDomain::BufferAdaptor &bufferAdaptor, Akonadi2::Storage::Transaction &transaction)
{
    for (const auto &property : mProperties) {
        const auto value = bufferAdaptor.getProperty(property);
        auto indexer = mIndexer.value(property);
        indexer(identifier, value, transaction);
    }
}

void TypeIndex::remove(const QByteArray &identifier, const Akonadi2::ApplicationDomain::BufferAdaptor &bufferAdaptor, Akonadi2::Storage::Transaction &transaction)
{
    for (const auto &property : mProperties) {
        const auto value = bufferAdaptor.getProperty(property);
        if (value.isValid()) {
            //FIXME don't always convert to byte array
            Index(mType + ".index." + property, transaction).remove(value.toByteArray(), identifier);
        } else {
            Index(mType + ".index." + property, transaction).remove("toplevel", identifier);
        }
    }
}

ResultSet TypeIndex::query(const Akonadi2::Query &query, QSet<QByteArray> &appliedFilters, Akonadi2::Storage::Transaction &transaction)
{
    QVector<QByteArray> keys;
    for (const auto &property : mProperties) {
        if (query.propertyFilter.contains(property)) {
            Index index(mType + ".index." + property, transaction);
            auto lookupKey = query.propertyFilter.value(property).toByteArray();
            if (lookupKey.isEmpty()) {
                lookupKey = "toplevel";
            }
            index.lookup(lookupKey, [&](const QByteArray &value) {
                keys << value;
            },
            [property](const Index::Error &error) {
                Warning() << "Error in index: " <<  error.message << property;
            });
            appliedFilters << property;
        }
        Trace() << "Index lookup on " << property << " found " << keys.size() << " keys.";
        return ResultSet(keys);
    }
    Trace() << "No matching index";
    return ResultSet(keys);
}

