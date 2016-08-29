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
#include "synchronizer.h"

#include "definitions.h"
#include "commands.h"
#include "bufferutils.h"
#include "entitystore.h"
#include "remoteidmap.h"
#include "adaptorfactoryregistry.h"
#include "createentity_generated.h"
#include "modifyentity_generated.h"
#include "deleteentity_generated.h"

SINK_DEBUG_AREA("synchronizer")

using namespace Sink;

Synchronizer::Synchronizer(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier)
    : mStorage(Sink::storageLocation(), resourceInstanceIdentifier, Sink::Storage::ReadOnly),
    mSyncStorage(Sink::storageLocation(), resourceInstanceIdentifier + ".synchronization", Sink::Storage::ReadWrite),
    mResourceType(resourceType),
    mResourceInstanceIdentifier(resourceInstanceIdentifier)
{
    SinkTrace() << "Starting synchronizer: " << resourceType << resourceInstanceIdentifier;
}

Synchronizer::~Synchronizer()
{

}

void Synchronizer::setup(const std::function<void(int commandId, const QByteArray &data)> &enqueueCommandCallback, MessageQueue &mq)
{
    mEnqueue = enqueueCommandCallback;
    mMessageQueue = &mq;
}

void Synchronizer::enqueueCommand(int commandId, const QByteArray &data)
{
    Q_ASSERT(mEnqueue);
    mEnqueue(commandId, data);
}

EntityStore &Synchronizer::store()
{
    if (!mEntityStore) {
        mEntityStore = QSharedPointer<EntityStore>::create(mResourceType, mResourceInstanceIdentifier, transaction());
    }
    return *mEntityStore;
}

RemoteIdMap &Synchronizer::syncStore()
{
    if (!mSyncStore) {
        mSyncStore = QSharedPointer<RemoteIdMap>::create(syncTransaction());
    }
    return *mSyncStore;
}

void Synchronizer::createEntity(const QByteArray &sinkId, const QByteArray &bufferType, const Sink::ApplicationDomain::ApplicationDomainType &domainObject,
    DomainTypeAdaptorFactoryInterface &adaptorFactory, std::function<void(const QByteArray &)> callback)
{
    // These changes are coming from the source
    const auto replayToSource = false;
    flatbuffers::FlatBufferBuilder entityFbb;
    adaptorFactory.createBuffer(domainObject, entityFbb);
    flatbuffers::FlatBufferBuilder fbb;
    // This is the resource type and not the domain type
    auto entityId = fbb.CreateString(sinkId.toStdString());
    auto type = fbb.CreateString(bufferType.toStdString());
    auto delta = Sink::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
    auto location = Sink::Commands::CreateCreateEntity(fbb, entityId, type, delta, replayToSource);
    Sink::Commands::FinishCreateEntityBuffer(fbb, location);
    callback(BufferUtils::extractBuffer(fbb));
}

void Synchronizer::modifyEntity(const QByteArray &sinkId, qint64 revision, const QByteArray &bufferType, const Sink::ApplicationDomain::ApplicationDomainType &domainObject,
    DomainTypeAdaptorFactoryInterface &adaptorFactory, std::function<void(const QByteArray &)> callback)
{
    // FIXME removals
    QByteArrayList deletedProperties;
    // These changes are coming from the source
    const auto replayToSource = false;
    flatbuffers::FlatBufferBuilder entityFbb;
    adaptorFactory.createBuffer(domainObject, entityFbb);
    flatbuffers::FlatBufferBuilder fbb;
    auto entityId = fbb.CreateString(sinkId.toStdString());
    auto modifiedProperties = BufferUtils::toVector(fbb, domainObject.changedProperties());
    auto deletions = BufferUtils::toVector(fbb, deletedProperties);
    // This is the resource type and not the domain type
    auto type = fbb.CreateString(bufferType.toStdString());
    auto delta = Sink::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
    auto location = Sink::Commands::CreateModifyEntity(fbb, revision, entityId, deletions, type, delta, replayToSource, modifiedProperties);
    Sink::Commands::FinishModifyEntityBuffer(fbb, location);
    callback(BufferUtils::extractBuffer(fbb));
}

void Synchronizer::deleteEntity(const QByteArray &sinkId, qint64 revision, const QByteArray &bufferType, std::function<void(const QByteArray &)> callback)
{
    // These changes are coming from the source
    const auto replayToSource = false;
    flatbuffers::FlatBufferBuilder fbb;
    auto entityId = fbb.CreateString(sinkId.toStdString());
    // This is the resource type and not the domain type
    auto type = fbb.CreateString(bufferType.toStdString());
    auto location = Sink::Commands::CreateDeleteEntity(fbb, revision, entityId, type, replayToSource);
    Sink::Commands::FinishDeleteEntityBuffer(fbb, location);
    callback(BufferUtils::extractBuffer(fbb));
}

void Synchronizer::scanForRemovals(const QByteArray &bufferType, const std::function<void(const std::function<void(const QByteArray &key)> &callback)> &entryGenerator, std::function<bool(const QByteArray &remoteId)> exists)
{
    entryGenerator([this, bufferType, &exists](const QByteArray &key) {
        auto sinkId = Sink::Storage::uidFromKey(key);
        const auto remoteId = syncStore().resolveLocalId(bufferType, sinkId);
        SinkTrace() << "Checking for removal " << key << remoteId;
        // If we have no remoteId, the entity hasn't been replayed to the source yet
        if (!remoteId.isEmpty()) {
            if (!exists(remoteId)) {
                SinkTrace() << "Found a removed entity: " << sinkId;
                deleteEntity(sinkId, Sink::Storage::maxRevision(transaction()), bufferType,
                    [this](const QByteArray &buffer) { enqueueCommand(Sink::Commands::DeleteEntityCommand, buffer); });
            }
        }
    });
}

void Synchronizer::modify(const QByteArray &bufferType, const QByteArray &remoteId, const Sink::ApplicationDomain::ApplicationDomainType &entity)
{
    auto mainDatabase = Storage::mainDatabase(transaction(), bufferType);
    const auto sinkId = syncStore().resolveRemoteId(bufferType, remoteId);
    auto adaptorFactory = Sink::AdaptorFactoryRegistry::instance().getFactory(mResourceType, bufferType);
    Q_ASSERT(adaptorFactory);
    qint64 retrievedRevision = 0;
    if (auto current = EntityReaderUtils::getLatest(mainDatabase, sinkId, *adaptorFactory, retrievedRevision)) {
        bool changed = false;
        for (const auto &property : entity.changedProperties()) {
            if (entity.getProperty(property) != current->getProperty(property)) {
                SinkTrace() << "Property changed " << sinkId << property;
                changed = true;
            }
        }
        if (changed) {
            SinkTrace() << "Found a modified entity: " << remoteId;
            modifyEntity(sinkId, Sink::Storage::maxRevision(transaction()), bufferType, entity, *adaptorFactory,
                [this](const QByteArray &buffer) { enqueueCommand(Sink::Commands::ModifyEntityCommand, buffer); });
        }
    } else {
        SinkWarning() << "Failed to get current entity";
    }
}

void Synchronizer::createOrModify(const QByteArray &bufferType, const QByteArray &remoteId, const Sink::ApplicationDomain::ApplicationDomainType &entity)
{
    SinkTrace() << "Create or modify" << bufferType << remoteId;
    auto mainDatabase = Storage::mainDatabase(transaction(), bufferType);
    const auto sinkId = syncStore().resolveRemoteId(bufferType, remoteId);
    const auto found = mainDatabase.contains(sinkId);
    if (!found) {
        SinkTrace() << "Found a new entity: " << remoteId;
        auto adaptorFactory = Sink::AdaptorFactoryRegistry::instance().getFactory(mResourceType, bufferType);
        Q_ASSERT(adaptorFactory);
        createEntity(
            sinkId, bufferType, entity, *adaptorFactory, [this](const QByteArray &buffer) { enqueueCommand(Sink::Commands::CreateEntityCommand, buffer); });
    } else { // modification
        modify(bufferType, remoteId, entity);
    }
}

template<typename DomainType>
void Synchronizer::createOrModify(const QByteArray &bufferType, const QByteArray &remoteId, const DomainType &entity, const QHash<QByteArray, Sink::Query::Comparator> &mergeCriteria)
{

    SinkTrace() << "Create or modify" << bufferType << remoteId;
    auto mainDatabase = Storage::mainDatabase(transaction(), bufferType);
    const auto sinkId = syncStore().resolveRemoteId(bufferType, remoteId);
    const auto found = mainDatabase.contains(sinkId);
    auto adaptorFactory = Sink::AdaptorFactoryRegistry::instance().getFactory(mResourceType, bufferType);
    if (!found) {
        if (!mergeCriteria.isEmpty()) {
            Sink::Query query;
            query.propertyFilter = mergeCriteria;
            bool merge = false;
            Sink::EntityReader<DomainType> reader(mResourceType, mResourceInstanceIdentifier, transaction());
            reader.query(query,
                [this, bufferType, remoteId, &merge](const DomainType &o) -> bool{
                    merge = true;
                    SinkTrace() << "Merging local entity with remote entity: " << o.identifier() << remoteId;
                    syncStore().recordRemoteId(bufferType, o.identifier(), remoteId);
                    return false;
                });
            if (!merge) {
                SinkTrace() << "Found a new entity: " << remoteId;
                createEntity(
                    sinkId, bufferType, entity, *adaptorFactory, [this](const QByteArray &buffer) { enqueueCommand(Sink::Commands::CreateEntityCommand, buffer); });
            }
        } else {
            SinkTrace() << "Found a new entity: " << remoteId;
            createEntity(
                sinkId, bufferType, entity, *adaptorFactory, [this](const QByteArray &buffer) { enqueueCommand(Sink::Commands::CreateEntityCommand, buffer); });
        }
    } else { // modification
        qint64 retrievedRevision = 0;
        if (auto current = EntityReaderUtils::getLatest(mainDatabase, sinkId, *adaptorFactory, retrievedRevision)) {
            bool changed = false;
            for (const auto &property : entity.changedProperties()) {
                if (entity.getProperty(property) != current->getProperty(property)) {
                    SinkTrace() << "Property changed " << sinkId << property;
                    changed = true;
                }
            }
            if (changed) {
                SinkTrace() << "Found a modified entity: " << remoteId;
                modifyEntity(sinkId, Sink::Storage::maxRevision(transaction()), bufferType, entity, *adaptorFactory,
                    [this](const QByteArray &buffer) { enqueueCommand(Sink::Commands::ModifyEntityCommand, buffer); });
            }
        } else {
            SinkWarning() << "Failed to get current entity";
        }
    }
}

template<typename DomainType>
void Synchronizer::modify(const DomainType &entity)
{
    const auto bufferType = ApplicationDomain::getTypeName<DomainType>();
    const auto adaptorFactory = Sink::AdaptorFactoryRegistry::instance().getFactory(mResourceType, bufferType);
    Q_ASSERT(adaptorFactory);
    modifyEntity(entity.identifier(), entity.revision(), bufferType, entity, *adaptorFactory,
                    [this](const QByteArray &buffer) { enqueueCommand(Sink::Commands::ModifyEntityCommand, buffer); });
}

KAsync::Job<void> Synchronizer::synchronize()
{
    SinkTrace() << "Synchronizing";
    mSyncInProgress = true;
    mMessageQueue->startTransaction();
    return synchronizeWithSource().syncThen<void>([this]() {
        mSyncStore.clear();
        mEntityStore.clear();
        mMessageQueue->commit();
        mSyncInProgress = false;
    });
}

void Synchronizer::commit()
{
    mMessageQueue->commit();
    mTransaction.abort();
    mEntityStore.clear();
    mSyncTransaction.commit();
    mSyncStore.clear();
    if (mSyncInProgress) {
        mMessageQueue->startTransaction();
    }
}

Sink::Storage::Transaction &Synchronizer::transaction()
{
    if (!mTransaction) {
        SinkTrace() << "Starting transaction";
        mTransaction = mStorage.createTransaction(Sink::Storage::ReadOnly);
    }
    return mTransaction;
}

Sink::Storage::Transaction &Synchronizer::syncTransaction()
{
    if (!mSyncTransaction) {
        SinkTrace() << "Starting transaction";
        mSyncTransaction = mSyncStorage.createTransaction(Sink::Storage::ReadWrite);
    }
    return mSyncTransaction;
}

#define REGISTER_TYPE(T)                                                          \
    template void Synchronizer::createOrModify(const QByteArray &bufferType, const QByteArray &remoteId, const T &entity, const QHash<QByteArray, Sink::Query::Comparator> &mergeCriteria); \
    template void Synchronizer::modify(const T &entity);

REGISTER_TYPE(ApplicationDomain::Event);
REGISTER_TYPE(ApplicationDomain::Mail);
REGISTER_TYPE(ApplicationDomain::Folder);

