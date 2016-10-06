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
#pragma once

#include "query.h"
#include "storage.h"
#include "resultset.h"
#include "typeindex.h"
#include "query.h"
#include "entitybuffer.h"
#include "log.h"


class Source;
class FilterBase;

class DataStoreQuery {
    friend class FilterBase;
public:
    typedef QSharedPointer<DataStoreQuery> Ptr;

    DataStoreQuery(const Sink::Query &query, const QByteArray &type, Sink::Storage::Transaction &transaction, TypeIndex &typeIndex, std::function<QVariant(const Sink::Entity &entity, const QByteArray &property)> getProperty);
    ResultSet execute();
    ResultSet update(qint64 baseRevision);

protected:

    typedef std::function<bool(const QByteArray &uid, const Sink::EntityBuffer &entityBuffer)> FilterFunction;
    typedef std::function<void(const QByteArray &uid, const Sink::EntityBuffer &entityBuffer)> BufferCallback;

    virtual QVariant getProperty(const Sink::Entity &entity, const QByteArray &property);
    QVector<QByteArray> indexLookup(const QByteArray &property, const QVariant &value);

    virtual void readEntity(const QByteArray &key, const BufferCallback &resultCallback);

    ResultSet createFilteredSet(ResultSet &resultSet, const std::function<bool(const QByteArray &, const Sink::EntityBuffer &buffer)> &);
    QVector<QByteArray> loadIncrementalResultSet(qint64 baseRevision);

    void setupQuery();
    QByteArrayList executeSubquery(const Sink::Query &subquery);

    Sink::Query mQuery;
    Sink::Storage::Transaction &mTransaction;
    const QByteArray mType;
    TypeIndex &mTypeIndex;
    Sink::Storage::NamedDatabase mDb;
    std::function<QVariant(const Sink::Entity &entity, const QByteArray &property)> mGetProperty;
    bool mInitialQuery;
    QSharedPointer<FilterBase> mCollector;
    QSharedPointer<Source> mSource;

    SINK_DEBUG_COMPONENT(mType)
};


class FilterBase {
public:
    typedef QSharedPointer<FilterBase> Ptr;
    FilterBase(DataStoreQuery *store)
        : mDatastore(store)
    {

    }

    FilterBase(FilterBase::Ptr source, DataStoreQuery *store)
        : mSource(source),
        mDatastore(store)
    {
    }

    virtual ~FilterBase(){}

    void readEntity(const QByteArray &key, const std::function<void(const QByteArray &, const Sink::EntityBuffer &buffer)> &callback)
    {
        Q_ASSERT(mDatastore);
        mDatastore->readEntity(key, callback);
    }

    QVariant getProperty(const Sink::Entity &entity, const QByteArray &property)
    {
        Q_ASSERT(mDatastore);
        return mDatastore->getProperty(entity, property);
    }

    QVector<QByteArray> indexLookup(const QByteArray &property, const QVariant &value)
    {
        Q_ASSERT(mDatastore);
        return mDatastore->indexLookup(property, value);
    }

    virtual void skip() { mSource->skip(); };

    //Returns true for as long as a result is available
    virtual bool next(const std::function<void(const ResultSet::Result &)> &callback) = 0;

    QSharedPointer<FilterBase> mSource;
    DataStoreQuery *mDatastore;
};

