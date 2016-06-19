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
#include <QObject>
#include <Async/Async>
#include <domainadaptor.h>
#include <query.h>
#include <messagequeue.h>
#include <storage.h>

namespace Sink {
class EntityStore;
class RemoteIdMap;

/**
 * Synchronize and add what we don't already have to local queue
 */
class SINK_EXPORT Synchronizer
{
public:
    Synchronizer(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier);
    virtual ~Synchronizer();

    void setup(const std::function<void(int commandId, const QByteArray &data)> &enqueueCommandCallback, MessageQueue &messageQueue);
    KAsync::Job<void> synchronize();

    //Read only access to main storage
    EntityStore &store();

    //Read/Write access to sync storage
    RemoteIdMap &syncStore();

    void commit();
    Sink::Storage::Transaction &transaction();
    Sink::Storage::Transaction &syncTransaction();

protected:
    ///Calls the callback to enqueue the command
    void enqueueCommand(int commandId, const QByteArray &data);

    static void createEntity(const QByteArray &localId, const QByteArray &bufferType, const Sink::ApplicationDomain::ApplicationDomainType &domainObject,
        DomainTypeAdaptorFactoryInterface &adaptorFactory, std::function<void(const QByteArray &)> callback);
    static void modifyEntity(const QByteArray &localId, qint64 revision, const QByteArray &bufferType, const Sink::ApplicationDomain::ApplicationDomainType &domainObject,
        DomainTypeAdaptorFactoryInterface &adaptorFactory, std::function<void(const QByteArray &)> callback);
    static void deleteEntity(const QByteArray &localId, qint64 revision, const QByteArray &bufferType, std::function<void(const QByteArray &)> callback);

    /**
    * A synchronous algorithm to remove entities that are no longer existing.
    *
    * A list of entities is generated by @param entryGenerator.
    * The entiry Generator typically iterates over an index to produce all existing entries.
    * This algorithm calls @param exists for every entity of type @param type, with its remoteId. For every entity where @param exists returns false,
    * an entity delete command is enqueued.
    *
    * All functions are called synchronously, and both @param entryGenerator and @param exists need to be synchronous.
    */
    void scanForRemovals(const QByteArray &bufferType,
        const std::function<void(const std::function<void(const QByteArray &key)> &callback)> &entryGenerator, std::function<bool(const QByteArray &remoteId)> exists);

    /**
     * An algorithm to create or modify the entity.
     *
     * Depending on whether the entity is locally available, or has changed.
     */
    void createOrModify(const QByteArray &bufferType, const QByteArray &remoteId, const Sink::ApplicationDomain::ApplicationDomainType &entity);
    template <typename DomainType>
    void createOrModify(const QByteArray &bufferType, const QByteArray &remoteId, const DomainType &entity, const QHash<QByteArray, Sink::Query::Comparator> &mergeCriteria);

    // template <typename DomainType>
    // void create(const DomainType &entity);
    template <typename DomainType>
    void modify(const DomainType &entity);
    // template <typename DomainType>
    // void remove(const DomainType &entity);

    virtual KAsync::Job<void> synchronizeWithSource() = 0;

private:
    QSharedPointer<RemoteIdMap> mSyncStore;
    QSharedPointer<EntityStore> mEntityStore;
    Sink::Storage mStorage;
    Sink::Storage mSyncStorage;
    QByteArray mResourceType;
    QByteArray mResourceInstanceIdentifier;
    Sink::Storage::Transaction mTransaction;
    Sink::Storage::Transaction mSyncTransaction;
    std::function<void(int commandId, const QByteArray &data)> mEnqueue;
    MessageQueue *mMessageQueue;
    bool mSyncInProgress;
};

}

