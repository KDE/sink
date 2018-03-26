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

#include <memory>
#include "domaintypeadaptorfactoryinterface.h"
#include "query.h"
#include "storage.h"
#include "resourcecontext.h"
#include "metadata_generated.h"

namespace Sink {
class EntityBuffer;
namespace Storage {

class SINK_EXPORT EntityStore
{
public:
    typedef QSharedPointer<EntityStore> Ptr;
    EntityStore(const ResourceContext &resourceContext, const Sink::Log::Context &);
    ~EntityStore() = default;

    void createIfMissing();

    //Only the pipeline may call the following functions outside of tests
    bool add(const QByteArray &type, ApplicationDomain::ApplicationDomainType newEntity, bool replayToSource);
    bool modify(const QByteArray &type, const ApplicationDomain::ApplicationDomainType &diff, const QByteArrayList &deletions, bool replayToSource);
    bool modify(const QByteArray &type, const ApplicationDomain::ApplicationDomainType &current, ApplicationDomain::ApplicationDomainType newEntity, bool replayToSource);
    bool remove(const QByteArray &type, const ApplicationDomain::ApplicationDomainType &current, bool replayToSource);
    bool cleanupRevisions(qint64 revision);
    ApplicationDomain::ApplicationDomainType applyDiff(const QByteArray &type, const ApplicationDomain::ApplicationDomainType &current, const ApplicationDomain::ApplicationDomainType &diff, const QByteArrayList &deletions) const;

    void startTransaction(Sink::Storage::DataStore::AccessMode);
    void commitTransaction();
    void abortTransaction();
    bool hasTransaction() const;

    QVector<QByteArray> fullScan(const QByteArray &type);
    QVector<QByteArray> indexLookup(const QByteArray &type, const QueryBase &query, QSet<QByteArray> &appliedFilters, QByteArray &appliedSorting);
    QVector<QByteArray> indexLookup(const QByteArray &type, const QByteArray &property, const QVariant &value);
    void indexLookup(const QByteArray &type, const QByteArray &property, const QVariant &value, const std::function<void(const QByteArray &uid)> &callback);
    template<typename EntityType, typename PropertyType>
    void indexLookup(const QVariant &value, const std::function<void(const QByteArray &uid)> &callback) {
        return indexLookup(ApplicationDomain::getTypeName<EntityType>(), PropertyType::name, value, callback);
    }

    void readLatest(const QByteArray &type, const QByteArray &uid, const std::function<void(const QByteArray &uid, const EntityBuffer &entity)> callback);
    void readLatest(const QByteArray &type, const QByteArray &uid, const std::function<void(const ApplicationDomain::ApplicationDomainType &entity)> callback);
    void readLatest(const QByteArray &type, const QByteArray &uid, const std::function<void(const ApplicationDomain::ApplicationDomainType &entity, Sink::Operation)> callback);

    ApplicationDomain::ApplicationDomainType readLatest(const QByteArray &type, const QByteArray &uid);

    template<typename T>
    T readLatest(const QByteArray &uid) {
        return T(readLatest(ApplicationDomain::getTypeName<T>(), uid));
    }

    void readEntity(const QByteArray &type, const QByteArray &uid, const std::function<void(const QByteArray &uid, const EntityBuffer &entity)> callback);
    void readEntity(const QByteArray &type, const QByteArray &uid, const std::function<void(const ApplicationDomain::ApplicationDomainType &entity)> callback);
    ApplicationDomain::ApplicationDomainType readEntity(const QByteArray &type, const QByteArray &key);

    template<typename T>
    T readEntity(const QByteArray &key) {
        return T(readEntity(ApplicationDomain::getTypeName<T>(), key));
    }


    void readPrevious(const QByteArray &type, const QByteArray &uid, qint64 revision, const std::function<void(const QByteArray &uid, const EntityBuffer &entity)> callback);
    void readPrevious(const QByteArray &type, const QByteArray &uid, qint64 revision, const std::function<void(const ApplicationDomain::ApplicationDomainType &entity)> callback);
    ApplicationDomain::ApplicationDomainType readPrevious(const QByteArray &type, const QByteArray &uid, qint64 revision);

    template<typename T>
    T readPrevious(const QByteArray &uid, qint64 revision) {
        return T(readPrevious(ApplicationDomain::getTypeName<T>(), uid, revision));
    }

    void readAllUids(const QByteArray &type, const std::function<void(const QByteArray &uid)> callback);

    void readAll(const QByteArray &type, const std::function<void(const ApplicationDomain::ApplicationDomainType &entity)> &callback);

    template<typename T>
    void readAll(const std::function<void(const T &entity)> &callback) {
        return readAll(ApplicationDomain::getTypeName<T>(), [&](const ApplicationDomain::ApplicationDomainType &entity) {
            callback(T(entity));
        });
    }

    void readRevisions(qint64 baseRevision, const QByteArray &type, const std::function<void(const QByteArray &key)> &callback);

    ///Db contains entity (but may already be marked as removed
    bool contains(const QByteArray &type, const QByteArray &uid);

    ///Db contains entity and entity is not yet removed
    bool exists(const QByteArray &type, const QByteArray &uid);

    qint64 maxRevision();

    Sink::Log::Context logContext() const;

private:
    /*
     * Remove any old revisions of the same entity up until @param revision
     */
    void cleanupEntityRevisionsUntil(qint64 revision);
    void copyBlobs(ApplicationDomain::ApplicationDomainType &entity, qint64 newRevision);
    class Private;
    const QSharedPointer<Private> d;
};

}
}
