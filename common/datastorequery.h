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

#include "sink_export.h"

#include "query.h"
#include "resultset.h"
#include "log.h"
#include "storage/entitystore.h"
#include "storage/key.h"

class Source;
class Bloom;
class Reduce;
class Filter;
class FilterBase;

class SINK_EXPORT DataStoreQuery {
    friend class FilterBase;
    friend class Source;
    friend class Bloom;
    friend class Reduce;
    friend class Filter;
public:
    typedef QSharedPointer<DataStoreQuery> Ptr;

    struct State {
        typedef QSharedPointer<State> Ptr;
        QSharedPointer<FilterBase> mCollector;
        QSharedPointer<Source> mSource;
    };

    DataStoreQuery(const Sink::QueryBase &query, const QByteArray &type, Sink::Storage::EntityStore &store);
    DataStoreQuery(const DataStoreQuery::State &state, const QByteArray &type, Sink::Storage::EntityStore &store, bool incremental);
    ~DataStoreQuery();
    ResultSet execute();
    ResultSet update(qint64 baseRevision);
    void updateComplete();

    State::Ptr getState();

private:

    typedef std::function<bool(const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Operation)> FilterFunction;
    typedef std::function<void(const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Operation)> BufferCallback;

    QVector<Sink::Storage::Identifier> indexLookup(const QByteArray &property, const QVariant &value, const QVector<Sink::Storage::Identifier> &filter = {});

    void readEntity(const Sink::Storage::Identifier &id, const BufferCallback &resultCallback);
    void readPrevious(const Sink::Storage::Identifier &id, const std::function<void (const Sink::ApplicationDomain::ApplicationDomainType &)> &callback);

    ResultSet createFilteredSet(ResultSet &resultSet, const FilterFunction &);
    QVector<Sink::Storage::Key> loadIncrementalResultSet(qint64 baseRevision);

    void setupQuery(const Sink::QueryBase &query_);
    QByteArrayList executeSubquery(const Sink::QueryBase &subquery);

    const QByteArray mType;
    QSharedPointer<FilterBase> mCollector;
    QSharedPointer<Source> mSource;

    Sink::Storage::EntityStore &mStore;
    Sink::Log::Context mLogCtx;
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

    void readEntity(const Sink::Storage::Identifier &id, const std::function<void(const Sink::ApplicationDomain::ApplicationDomainType &entity, Sink::Operation)> &callback)
    {
        Q_ASSERT(mDatastore);
        mDatastore->readEntity(id, callback);
    }

    QVector<Sink::Storage::Identifier> indexLookup(const QByteArray &property, const QVariant &value, const QVector<Sink::Storage::Identifier> &filter = {})
    {
        Q_ASSERT(mDatastore);
        return mDatastore->indexLookup(property, value, filter);
    }

    void readPrevious(const Sink::Storage::Identifier &id, const std::function<void (const Sink::ApplicationDomain::ApplicationDomainType &)> &callback)
    {
        Q_ASSERT(mDatastore);
        mDatastore->readPrevious(id, callback);
    }

    virtual void skip() { mSource->skip(); }

    //Returns true for as long as a result is available
    virtual bool next(const std::function<void(const ResultSet::Result &)> &callback) = 0;

    virtual void updateComplete() { }

    FilterBase::Ptr mSource;
    DataStoreQuery *mDatastore{nullptr};
    bool mIncremental = false;
};

