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
#include "remoteidmap.h"
#include "datastorequery.h"
#include "createentity_generated.h"
#include "modifyentity_generated.h"
#include "deleteentity_generated.h"

SINK_DEBUG_AREA("synchronizer")

using namespace Sink;

Synchronizer::Synchronizer(const Sink::ResourceContext &context)
    : ChangeReplay(context),
    mResourceContext(context),
    mEntityStore(Storage::EntityStore::Ptr::create(mResourceContext)),
    mSyncStorage(Sink::storageLocation(), mResourceContext.instanceId() + ".synchronization", Sink::Storage::DataStore::DataStore::ReadWrite)
{
    SinkTrace() << "Starting synchronizer: " << mResourceContext.resourceType << mResourceContext.instanceId();
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

Storage::EntityStore &Synchronizer::store()
{
    mEntityStore->startTransaction(Sink::Storage::DataStore::ReadOnly);
    return *mEntityStore;
}

RemoteIdMap &Synchronizer::syncStore()
{
    if (!mSyncStore) {
        mSyncStore = QSharedPointer<RemoteIdMap>::create(syncTransaction());
    }
    return *mSyncStore;
}

void Synchronizer::createEntity(const QByteArray &sinkId, const QByteArray &bufferType, const Sink::ApplicationDomain::ApplicationDomainType &domainObject)
{
    // These changes are coming from the source
    const auto replayToSource = false;
    flatbuffers::FlatBufferBuilder entityFbb;
    mResourceContext.adaptorFactory(bufferType).createBuffer(domainObject, entityFbb);
    flatbuffers::FlatBufferBuilder fbb;
    // This is the resource type and not the domain type
    auto entityId = fbb.CreateString(sinkId.toStdString());
    auto type = fbb.CreateString(bufferType.toStdString());
    auto delta = Sink::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
    auto location = Sink::Commands::CreateCreateEntity(fbb, entityId, type, delta, replayToSource);
    Sink::Commands::FinishCreateEntityBuffer(fbb, location);
    enqueueCommand(Sink::Commands::CreateEntityCommand, BufferUtils::extractBuffer(fbb));
}

void Synchronizer::modifyEntity(const QByteArray &sinkId, qint64 revision, const QByteArray &bufferType, const Sink::ApplicationDomain::ApplicationDomainType &domainObject)
{
    // FIXME removals
    QByteArrayList deletedProperties;
    // These changes are coming from the source
    const auto replayToSource = false;
    flatbuffers::FlatBufferBuilder entityFbb;
    mResourceContext.adaptorFactory(bufferType).createBuffer(domainObject, entityFbb);
    flatbuffers::FlatBufferBuilder fbb;
    auto entityId = fbb.CreateString(sinkId.toStdString());
    auto modifiedProperties = BufferUtils::toVector(fbb, domainObject.changedProperties());
    auto deletions = BufferUtils::toVector(fbb, deletedProperties);
    // This is the resource type and not the domain type
    auto type = fbb.CreateString(bufferType.toStdString());
    auto delta = Sink::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
    auto location = Sink::Commands::CreateModifyEntity(fbb, revision, entityId, deletions, type, delta, replayToSource, modifiedProperties);
    Sink::Commands::FinishModifyEntityBuffer(fbb, location);
    enqueueCommand(Sink::Commands::ModifyEntityCommand, BufferUtils::extractBuffer(fbb));
}

void Synchronizer::deleteEntity(const QByteArray &sinkId, qint64 revision, const QByteArray &bufferType)
{
    // These changes are coming from the source
    const auto replayToSource = false;
    flatbuffers::FlatBufferBuilder fbb;
    auto entityId = fbb.CreateString(sinkId.toStdString());
    // This is the resource type and not the domain type
    auto type = fbb.CreateString(bufferType.toStdString());
    auto location = Sink::Commands::CreateDeleteEntity(fbb, revision, entityId, type, replayToSource);
    Sink::Commands::FinishDeleteEntityBuffer(fbb, location);
    enqueueCommand(Sink::Commands::DeleteEntityCommand, BufferUtils::extractBuffer(fbb));
}

void Synchronizer::scanForRemovals(const QByteArray &bufferType, const std::function<void(const std::function<void(const QByteArray &key)> &callback)> &entryGenerator, std::function<bool(const QByteArray &remoteId)> exists)
{
    entryGenerator([this, bufferType, &exists](const QByteArray &sinkId) {
        const auto remoteId = syncStore().resolveLocalId(bufferType, sinkId);
        SinkTrace() << "Checking for removal " << sinkId << remoteId;
        // If we have no remoteId, the entity hasn't been replayed to the source yet
        if (!remoteId.isEmpty()) {
            if (!exists(remoteId)) {
                SinkTrace() << "Found a removed entity: " << sinkId;
                deleteEntity(sinkId, mEntityStore->maxRevision(), bufferType);
            }
        }
    });
}

void Synchronizer::scanForRemovals(const QByteArray &bufferType, std::function<bool(const QByteArray &remoteId)> exists)
{
    scanForRemovals(bufferType,
        [this, &bufferType](const std::function<void(const QByteArray &)> &callback) {
            store().readAllUids(bufferType, [callback](const QByteArray &uid) {
                callback(uid);
            });
        },
        exists
    );
}

void Synchronizer::modifyIfChanged(Storage::EntityStore &store, const QByteArray &bufferType, const QByteArray &sinkId, const Sink::ApplicationDomain::ApplicationDomainType &entity)
{
    store.readLatest(bufferType, sinkId, [&, this](const Sink::ApplicationDomain::ApplicationDomainType &current) {
        bool changed = false;
        for (const auto &property : entity.changedProperties()) {
            if (entity.getProperty(property) != current.getProperty(property)) {
                SinkTrace() << "Property changed " << sinkId << property;
                changed = true;
            }
        }
        if (changed) {
            SinkTrace() << "Found a modified entity: " << sinkId;
            modifyEntity(sinkId, store.maxRevision(), bufferType, entity);
        }
    });
}

void Synchronizer::modify(const QByteArray &bufferType, const QByteArray &remoteId, const Sink::ApplicationDomain::ApplicationDomainType &entity)
{
    const auto sinkId = syncStore().resolveRemoteId(bufferType, remoteId);
    Storage::EntityStore store(mResourceContext);
    modifyIfChanged(store, bufferType, sinkId, entity);
}

void Synchronizer::createOrModify(const QByteArray &bufferType, const QByteArray &remoteId, const Sink::ApplicationDomain::ApplicationDomainType &entity)
{
    SinkTrace() << "Create or modify" << bufferType << remoteId;
    Storage::EntityStore store(mResourceContext);
    const auto sinkId = syncStore().resolveRemoteId(bufferType, remoteId);
    const auto found = store.contains(bufferType, sinkId);
    if (!found) {
        SinkTrace() << "Found a new entity: " << remoteId;
        createEntity(sinkId, bufferType, entity);
    } else { // modification
        modify(bufferType, remoteId, entity);
    }
}

template<typename DomainType>
void Synchronizer::createOrModify(const QByteArray &bufferType, const QByteArray &remoteId, const DomainType &entity, const QHash<QByteArray, Sink::Query::Comparator> &mergeCriteria)
{

    SinkTrace() << "Create or modify" << bufferType << remoteId;
    const auto sinkId = syncStore().resolveRemoteId(bufferType, remoteId);
    Storage::EntityStore store(mResourceContext);
    const auto found = store.contains(bufferType, sinkId);
    if (!found) {
        if (!mergeCriteria.isEmpty()) {
            Sink::Query query;
            for (auto it = mergeCriteria.constBegin(); it != mergeCriteria.constEnd(); it++) {
                query.filter(it.key(), it.value());
            }
            bool merge = false;
            Storage::EntityStore store{mResourceContext};
            DataStoreQuery dataStoreQuery{query, ApplicationDomain::getTypeName<DomainType>(), store};
            auto resultSet = dataStoreQuery.execute();
            resultSet.replaySet(0, 1, [this, &merge, bufferType, remoteId](const ResultSet::Result &r) {
                merge = true;
                SinkTrace() << "Merging local entity with remote entity: " << r.entity.identifier() << remoteId;
                syncStore().recordRemoteId(bufferType, r.entity.identifier(), remoteId);
            });

            if (!merge) {
                SinkTrace() << "Found a new entity: " << remoteId;
                createEntity(sinkId, bufferType, entity);
            }
        } else {
            SinkTrace() << "Found a new entity: " << remoteId;
            createEntity(sinkId, bufferType, entity);
        }
    } else { // modification
        modifyIfChanged(store, bufferType, sinkId, entity);
    }
}

QByteArrayList Synchronizer::resolveFilter(const QueryBase::Comparator &filter)
{
    QByteArrayList result;
    if (filter.value.canConvert<QByteArray>()) {
        result << filter.value.value<QByteArray>();
    } else if (filter.value.canConvert<QueryBase>()) {
        auto query = filter.value.value<QueryBase>();
        Storage::EntityStore store{mResourceContext};
        DataStoreQuery dataStoreQuery{query, query.type(), store};
        auto resultSet = dataStoreQuery.execute();
        resultSet.replaySet(0, 0, [this, &result](const ResultSet::Result &r) {
            result << r.entity.identifier();
        });
    } else {
        SinkWarning() << "unknown filter type: " << filter;
        Q_ASSERT(false);
    }
    return result;
}

template<typename DomainType>
void Synchronizer::modify(const DomainType &entity)
{
    modifyEntity(entity.identifier(), entity.revision(), ApplicationDomain::getTypeName<DomainType>(), entity);
}

QList<Synchronizer::SyncRequest> Synchronizer::getSyncRequests(const Sink::QueryBase &query)
{
    QList<Synchronizer::SyncRequest> list;
    list << Synchronizer::SyncRequest{query};
    return list;
}

KAsync::Job<void> Synchronizer::synchronize(const Sink::QueryBase &query)
{
    SinkTrace() << "Synchronizing";
    mSyncRequestQueue << getSyncRequests(query);
    return processSyncQueue();
}

KAsync::Job<void> Synchronizer::processSyncQueue()
{
    if (mSyncRequestQueue.isEmpty() || mSyncInProgress) {
        return KAsync::null<void>();
    }
    mSyncInProgress = true;
    mMessageQueue->startTransaction();

    auto job = KAsync::null<void>();
    while (!mSyncRequestQueue.isEmpty()) {
        auto request = mSyncRequestQueue.takeFirst();
        job = job.then(synchronizeWithSource(request.query)).syncThen<void>([this] {
            //Commit after every request, so implementations only have to commit more if they add a lot of data.
            commit();
        });
    }
    return job.then<void>([this](const KAsync::Error &error) {
        mSyncStore.clear();
        mMessageQueue->commit();
        mSyncInProgress = false;
        if (error) {
            SinkWarning() << "Error during sync: " << error.errorMessage;
            return KAsync::error(error);
        }
        return KAsync::null<void>();
    });
}

void Synchronizer::commit()
{
    mMessageQueue->commit();
    mEntityStore->abortTransaction();
    mSyncTransaction.commit();
    mSyncStore.clear();
    if (mSyncInProgress) {
        mMessageQueue->startTransaction();
    }
}

Sink::Storage::DataStore::DataStore::Transaction &Synchronizer::syncTransaction()
{
    if (!mSyncTransaction) {
        SinkTrace() << "Starting transaction";
        mSyncTransaction = mSyncStorage.createTransaction(Sink::Storage::DataStore::DataStore::ReadWrite);
    }
    return mSyncTransaction;
}

bool Synchronizer::canReplay(const QByteArray &type, const QByteArray &key, const QByteArray &value)
{
    Sink::EntityBuffer buffer(value);
    const Sink::Entity &entity = buffer.entity();
    const auto metadataBuffer = Sink::EntityBuffer::readBuffer<Sink::Metadata>(entity.metadata());
    Q_ASSERT(metadataBuffer);
    if (!metadataBuffer->replayToSource()) {
        SinkTrace() << "Change is coming from the source";
    }
    return metadataBuffer->replayToSource();
}

KAsync::Job<void> Synchronizer::replay(const QByteArray &type, const QByteArray &key, const QByteArray &value)
{
    SinkTrace() << "Replaying" << type << key;

    Sink::EntityBuffer buffer(value);
    const Sink::Entity &entity = buffer.entity();
    const auto metadataBuffer = Sink::EntityBuffer::readBuffer<Sink::Metadata>(entity.metadata());
    Q_ASSERT(metadataBuffer);
    Q_ASSERT(!mSyncStore);
    Q_ASSERT(!mSyncTransaction);
    mEntityStore->startTransaction(Storage::DataStore::ReadOnly);
    mSyncTransaction = mSyncStorage.createTransaction(Sink::Storage::DataStore::ReadWrite);

    const auto operation = metadataBuffer ? metadataBuffer->operation() : Sink::Operation_Creation;
    const auto uid = Sink::Storage::DataStore::uidFromKey(key);
    const auto modifiedProperties = metadataBuffer->modifiedProperties() ? BufferUtils::fromVector(*metadataBuffer->modifiedProperties()) : QByteArrayList();
    QByteArray oldRemoteId;

    if (operation != Sink::Operation_Creation) {
        oldRemoteId = syncStore().resolveLocalId(type, uid);
        if (oldRemoteId.isEmpty()) {
            SinkWarning() << "Couldn't find the remote id for: " << type << uid;
            return KAsync::error<void>(1, "Couldn't find the remote id.");
        }
    }
    SinkTrace() << "Replaying " << key << type << uid << oldRemoteId;

    KAsync::Job<QByteArray> job = KAsync::null<QByteArray>();
    //TODO This requires supporting every domain type here as well. Can we solve this better so we can do the dispatch somewhere centrally?
    if (type == ApplicationDomain::getTypeName<ApplicationDomain::Folder>()) {
        auto folder = store().readEntity<ApplicationDomain::Folder>(key);
        job = replay(folder, operation, oldRemoteId, modifiedProperties);
    } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Mail>()) {
        auto mail = store().readEntity<ApplicationDomain::Mail>(key);
        job = replay(mail, operation, oldRemoteId, modifiedProperties);
    }

    return job.syncThen<void, QByteArray>([this, operation, type, uid, oldRemoteId](const QByteArray &remoteId) {
        if (operation == Sink::Operation_Creation) {
            SinkTrace() << "Replayed creation with remote id: " << remoteId;
            if (remoteId.isEmpty()) {
                SinkWarning() << "Returned an empty remoteId from the creation";
            } else {
                syncStore().recordRemoteId(type, uid, remoteId);
            }
        } else if (operation == Sink::Operation_Modification) {
            SinkTrace() << "Replayed modification with remote id: " << remoteId;
            if (remoteId.isEmpty()) {
                SinkWarning() << "Returned an empty remoteId from the creation";
            } else {
               syncStore().updateRemoteId(type, uid, remoteId);
            }
        } else if (operation == Sink::Operation_Removal) {
            SinkTrace() << "Replayed removal with remote id: " << oldRemoteId;
            syncStore().removeRemoteId(type, uid, oldRemoteId);
        } else {
            SinkError() << "Unkown operation" << operation;
        }
    })
    .syncThen<void>([this](const KAsync::Error &error) {
        if (error) {
            SinkWarning() << "Failed to replay change: " << error.errorMessage;
        }
        mSyncStore.clear();
        mSyncTransaction.commit();
        mEntityStore->abortTransaction();
    });
}

KAsync::Job<QByteArray> Synchronizer::replay(const ApplicationDomain::Mail &, Sink::Operation, const QByteArray &, const QList<QByteArray> &)
{
    return KAsync::null<QByteArray>();
}

KAsync::Job<QByteArray> Synchronizer::replay(const ApplicationDomain::Folder &, Sink::Operation, const QByteArray &, const QList<QByteArray> &)
{
    return KAsync::null<QByteArray>();
}

#define REGISTER_TYPE(T)                                                          \
    template void Synchronizer::createOrModify(const QByteArray &bufferType, const QByteArray &remoteId, const T &entity, const QHash<QByteArray, Sink::Query::Comparator> &mergeCriteria); \
    template void Synchronizer::modify(const T &entity);

REGISTER_TYPE(ApplicationDomain::Event);
REGISTER_TYPE(ApplicationDomain::Mail);
REGISTER_TYPE(ApplicationDomain::Folder);

