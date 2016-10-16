
/*
 * Copyright (C) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include "sink_export.h"
#include <domainadaptor.h>

#include "storage.h"
#include "resultprovider.h"
#include "adaptorfactoryregistry.h"

namespace Sink {

namespace EntityReaderUtils {
    SINK_EXPORT QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> getLatest(const Sink::Storage::DataStore::NamedDatabase &db, const QByteArray &uid, DomainTypeAdaptorFactoryInterface &adaptorFactory, qint64 &retrievedRevision);
    SINK_EXPORT QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> get(const Sink::Storage::DataStore::NamedDatabase &db, const QByteArray &key, DomainTypeAdaptorFactoryInterface &adaptorFactory, qint64 &retrievedRevision);
    SINK_EXPORT QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> getPrevious(const Sink::Storage::DataStore::NamedDatabase &db, const QByteArray &uid, qint64 revision, DomainTypeAdaptorFactoryInterface &adaptorFactory, qint64 &retrievedRevision);
};

/**
 * A synchronous interface to read entities from the storage.
 *
 * All callbacks will be called before the end of the function.
 * The caller must ensure passed in references remain valid for the lifetime of the object.
 *
 * This class is meant to be instantiated temporarily during reads on the stack.
 *
 * Note that all objects returned in callbacks are only valid during the execution of the callback and may start pointing into invalid memory if shallow-copied.
 */
template<typename DomainType>
class SINK_EXPORT EntityReader
{
    typedef std::function<bool(const typename DomainType::Ptr &domainObject, Sink::Operation operation, const QMap<QByteArray, QVariant> &aggregateValues)> ResultCallback;

public:
    EntityReader(Storage::EntityStore &store);

    /**
     * Reads the latest revision of an entity identified by @param uid
     */
    DomainType read(const QByteArray &uid) const;

    /**
     * Reads the revision of the entity identified by @param key (uid + revision)
     */
    DomainType readFromKey(const QByteArray &key) const;

    /**
     * Reads the (revision - 1) of an entity identified by @param uid
     */
    DomainType readPrevious(const QByteArray &uid, qint64 revision) const;

    /**
     * Reads all entities that match @param query.
     */
    void query(const Sink::Query &query, const std::function<bool(const DomainType &)> &callback);

    /**
     * Returns all entities that match @param query.
     * 
     * @param offset and @param batchsize can be used to return paginated results.
     */
    QPair<qint64, qint64> executeInitialQuery(const Sink::Query &query, int offset, int batchsize, const ResultCallback &callback);

    /**
     * Returns all changed entities that match @param query starting from @param lastRevision
     */
    QPair<qint64, qint64> executeIncrementalQuery(const Sink::Query &query, qint64 lastRevision, const ResultCallback &callback);

private:
    qint64 replaySet(ResultSet &resultSet, int offset, int batchSize, const ResultCallback &callback);

private:
    Sink::Storage::EntityStore &mEntityStore;
};

}
