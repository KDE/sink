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
#pragma once

#include "resultset.h"
#include "storage.h"
#include "query.h"
#include "log.h"
#include "indexer.h"
#include <QByteArray>

namespace Sink {
namespace Storage {
    class EntityStore;
}
}

class TypeIndex
{
public:
    TypeIndex(const QByteArray &type, const Sink::Log::Context &);

    template <typename T>
    void addProperty(const QByteArray &property);
    template <typename T, typename S>
    void addPropertyWithSorting(const QByteArray &property, const QByteArray &sortProperty);

    template <typename T, typename S>
    void addPropertyWithSorting()
    {
        addPropertyWithSorting<typename T::Type, typename S::Type>(T::name, S::name);
    }

    template <typename T>
    void addProperty()
    {
        addProperty<typename T::Type>(T::name);
    }

    template <typename T>
    void addPropertyWithSorting()
    {
        addPropertyWithSorting<typename T::Type>(T::name);
    }

    template <typename Left, typename Right>
    void addSecondaryProperty()
    {
        mSecondaryProperties.insert(Left::name, Right::name);
    }

    template <typename Left, typename Right, typename CustomIndexer>
    void addSecondaryPropertyIndexer()
    {
        mCustomIndexer << CustomIndexer::Ptr::create();
    }

    void add(const QByteArray &identifier, const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId);
    void remove(const QByteArray &identifier, const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId);

    QVector<QByteArray> query(const Sink::QueryBase &query, QSet<QByteArray> &appliedFilters, QByteArray &appliedSorting, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId);
    QVector<QByteArray> lookup(const QByteArray &property, const QVariant &value, Sink::Storage::DataStore::Transaction &transaction);

    template <typename Left, typename Right>
    QVector<QByteArray> secondaryLookup(const QVariant &value)
    {
        return secondaryLookup<typename Left::Type>(Left::name, Right::name, value);
    }

    template <typename Type>
    QVector<QByteArray> secondaryLookup(const QByteArray &leftName, const QByteArray &rightName, const QVariant &value);

    template <typename Left, typename Right>
    void index(const QVariant &leftValue, const QVariant &rightValue, Sink::Storage::DataStore::Transaction &transaction)
    {
        index<typename Left::Type, typename Right::Type>(Left::name, Right::name, leftValue, rightValue, transaction);
    }

    template <typename LeftType, typename RightType>
    void index(const QByteArray &leftName, const QByteArray &rightName, const QVariant &leftValue, const QVariant &rightValue, Sink::Storage::DataStore::Transaction &transaction);

    template <typename Left, typename Right>
    void unindex(const QVariant &leftValue, const QVariant &rightValue, Sink::Storage::DataStore::Transaction &transaction)
    {
        index<typename Left::Type, typename Right::Type>(Left::name, Right::name, leftValue, rightValue, transaction);
    }

    template <typename LeftType, typename RightType>
    void unindex(const QByteArray &leftName, const QByteArray &rightName, const QVariant &leftValue, const QVariant &rightValue, Sink::Storage::DataStore::Transaction &transaction);

    void commitTransaction();
    void abortTransaction();


private:
    friend class Sink::Storage::EntityStore;
    void updateIndex(bool add, const QByteArray &identifier, const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Storage::DataStore::Transaction &transaction, const QByteArray &resourceInstanceId);
    QByteArray indexName(const QByteArray &property, const QByteArray &sortProperty = QByteArray()) const;
    Sink::Log::Context mLogCtx;
    QByteArray mType;
    QByteArrayList mProperties;
    QMap<QByteArray, QByteArray> mSortedProperties;
    //<Property, ResultProperty>
    QMap<QByteArray, QByteArray> mSecondaryProperties;
    QList<Sink::Indexer::Ptr> mCustomIndexer;
    Sink::Storage::DataStore::Transaction *mTransaction;
    QHash<QByteArray, std::function<void(bool, const QByteArray &identifier, const QVariant &value, Sink::Storage::DataStore::Transaction &transaction)>> mIndexer;
    QHash<QByteArray, std::function<void(bool, const QByteArray &identifier, const QVariant &value, const QVariant &sortValue, Sink::Storage::DataStore::Transaction &transaction)>> mSortIndexer;
};
