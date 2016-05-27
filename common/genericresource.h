/*
 * Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include <resource.h>
#include <messagequeue.h>
#include <flatbuffers/flatbuffers.h>
#include <domainadaptor.h>
#include "changereplay.h"

#include <QTimer>

class CommandProcessor;

namespace Sink {
class Pipeline;
class Preprocessor;
class Synchronizer;

/**
 * Generic Resource implementation.
 */
class SINK_EXPORT GenericResource : public Resource
{
public:
    GenericResource(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier, const QSharedPointer<Pipeline> &pipeline, const QSharedPointer<ChangeReplay> &changeReplay, const QSharedPointer<Synchronizer> &synchronizer);
    virtual ~GenericResource();

    virtual void processCommand(int commandId, const QByteArray &data) Q_DECL_OVERRIDE;
    virtual KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE;
    virtual KAsync::Job<void> synchronizeWithSource(Sink::Storage &mainStore, Sink::Storage &synchronizationStore);
    virtual KAsync::Job<void> processAllMessages() Q_DECL_OVERRIDE;
    virtual void setLowerBoundRevision(qint64 revision) Q_DECL_OVERRIDE;
    virtual KAsync::Job<void>
    inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue);

    int error() const;

    void removeDataFromDisk() Q_DECL_OVERRIDE;
    static void removeFromDisk(const QByteArray &instanceIdentifier);
    static qint64 diskUsage(const QByteArray &instanceIdentifier);

private slots:
    void updateLowerBoundRevision();

protected:
    void enableChangeReplay(bool);

    void addType(const QByteArray &type, DomainTypeAdaptorFactoryInterface::Ptr factory, const QVector<Sink::Preprocessor *> &preprocessors);

    void onProcessorError(int errorCode, const QString &errorMessage);
    void enqueueCommand(MessageQueue &mq, int commandId, const QByteArray &data);

    MessageQueue mUserQueue;
    MessageQueue mSynchronizerQueue;
    QByteArray mResourceType;
    QByteArray mResourceInstanceIdentifier;
    QSharedPointer<Pipeline> mPipeline;

private:
    CommandProcessor *mProcessor;
    QSharedPointer<ChangeReplay> mChangeReplay;
    QSharedPointer<Synchronizer> mSynchronizer;
    int mError;
    QTimer mCommitQueueTimer;
    qint64 mClientLowerBoundRevision;
    QHash<QByteArray, DomainTypeAdaptorFactoryInterface::Ptr> mAdaptorFactories;
};

class SINK_EXPORT SyncStore
{
public:
    SyncStore(Sink::Storage::Transaction &);

    /**
     * Records a localId to remoteId mapping
     */
    void recordRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId);
    void removeRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId);
    void updateRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId);

    /**
     * Tries to find a local id for the remote id, and creates a new local id otherwise.
     *
     * The new local id is recorded in the local to remote id mapping.
     */
    QByteArray resolveRemoteId(const QByteArray &type, const QByteArray &remoteId);

    /**
     * Tries to find a remote id for a local id.
     *
     * This can fail if the entity hasn't been written back to the server yet.
     */
    QByteArray resolveLocalId(const QByteArray &bufferType, const QByteArray &localId);

private:
    Sink::Storage::Transaction &mTransaction;
};

class SINK_EXPORT EntityStore
{
public:
    EntityStore(const QByteArray &resourceType, const QByteArray &mResourceInstanceIdentifier, Sink::Storage::Transaction &transaction);

    template<typename T>
    T read(const QByteArray &identifier) const;

    static QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> getLatest(const Sink::Storage::NamedDatabase &db, const QByteArray &uid, DomainTypeAdaptorFactoryInterface &adaptorFactory);
private:
    QByteArray mResourceType;
    QByteArray mResourceInstanceIdentifier;
    Sink::Storage::Transaction &mTransaction;
};

/**
 * Synchronize and add what we don't already have to local queue
 */
class SINK_EXPORT Synchronizer
{
public:
    Synchronizer(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier);

    void setup(const std::function<void(int commandId, const QByteArray &data)> &enqueueCommandCallback);
    KAsync::Job<void> synchronize();

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

    //Read only access to main storage
    EntityStore &store();

    //Read/Write access to sync storage
    SyncStore &syncStore();

    virtual KAsync::Job<void> synchronizeWithSource() = 0;

private:
    QSharedPointer<SyncStore> mSyncStore;
    QSharedPointer<EntityStore> mEntityStore;
    Sink::Storage mStorage;
    Sink::Storage mSyncStorage;
    QByteArray mResourceType;
    QByteArray mResourceInstanceIdentifier;
    Sink::Storage::Transaction mTransaction;
    Sink::Storage::Transaction mSyncTransaction;
    std::function<void(int commandId, const QByteArray &data)> mEnqueue;
};

/**
 * Replay changes to the source
 */
class SINK_EXPORT SourceWriteBack : public ChangeReplay
{
public:
    SourceWriteBack(const QByteArray &resourceType,const QByteArray &resourceInstanceIdentifier);

protected:
    ///Base implementation calls the replay$Type calls
    virtual KAsync::Job<void> replay(const QByteArray &type, const QByteArray &key, const QByteArray &value) Q_DECL_OVERRIDE;

protected:
    ///Implement to write back changes to the server
    virtual KAsync::Job<QByteArray> replay(const Sink::ApplicationDomain::Mail &, Sink::Operation, const QByteArray &oldRemoteId);
    virtual KAsync::Job<QByteArray> replay(const Sink::ApplicationDomain::Folder &, Sink::Operation, const QByteArray &oldRemoteId);

    //Read only access to main storage
    EntityStore &store();

    //Read/Write access to sync storage
    SyncStore &syncStore();

private:
    Sink::Storage mSyncStorage;
    QSharedPointer<SyncStore> mSyncStore;
    QSharedPointer<EntityStore> mEntityStore;
    Sink::Storage::Transaction mTransaction;
    Sink::Storage::Transaction mSyncTransaction;
    QByteArray mResourceType;
    QByteArray mResourceInstanceIdentifier;
};


}
