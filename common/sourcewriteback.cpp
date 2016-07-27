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
#include "sourcewriteback.h"

#include "definitions.h"
#include "log.h"
#include "bufferutils.h"

#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

SINK_DEBUG_AREA("sourcewriteback")

using namespace Sink;

SourceWriteBack::SourceWriteBack(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier)
    : ChangeReplay(resourceInstanceIdentifier),
    mSyncStorage(Sink::storageLocation(), resourceInstanceIdentifier + ".synchronization", Sink::Storage::ReadWrite),
    mResourceType(resourceType),
    mResourceInstanceIdentifier(resourceInstanceIdentifier)
{

}

EntityStore &SourceWriteBack::store()
{
    if (!mEntityStore) {
        mEntityStore = QSharedPointer<EntityStore>::create(mResourceType, mResourceInstanceIdentifier, mTransaction);
    }
    return *mEntityStore;
}

RemoteIdMap &SourceWriteBack::syncStore()
{
    if (!mSyncStore) {
        mSyncStore = QSharedPointer<RemoteIdMap>::create(mSyncTransaction);
    }
    return *mSyncStore;
}

bool SourceWriteBack::canReplay(const QByteArray &type, const QByteArray &key, const QByteArray &value)
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

KAsync::Job<void> SourceWriteBack::replay(const QByteArray &type, const QByteArray &key, const QByteArray &value)
{
    SinkTrace() << "Replaying" << type << key;

    Sink::EntityBuffer buffer(value);
    const Sink::Entity &entity = buffer.entity();
    const auto metadataBuffer = Sink::EntityBuffer::readBuffer<Sink::Metadata>(entity.metadata());
    Q_ASSERT(metadataBuffer);
    Q_ASSERT(!mSyncStore);
    Q_ASSERT(!mEntityStore);
    Q_ASSERT(!mTransaction);
    Q_ASSERT(!mSyncTransaction);
    mTransaction = mStorage.createTransaction(Sink::Storage::ReadOnly);
    mSyncTransaction = mSyncStorage.createTransaction(Sink::Storage::ReadWrite);

    // const qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
    const auto operation = metadataBuffer ? metadataBuffer->operation() : Sink::Operation_Creation;
    const auto uid = Sink::Storage::uidFromKey(key);
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
    if (type == ENTITY_TYPE_FOLDER) {
        auto folder = store().readFromKey<ApplicationDomain::Folder>(key);
        job = replay(folder, operation, oldRemoteId, modifiedProperties);
    } else if (type == ENTITY_TYPE_MAIL) {
        auto mail = store().readFromKey<ApplicationDomain::Mail>(key);
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
        mEntityStore.clear();
        mTransaction.abort();
        mSyncTransaction.commit();
    });
}

KAsync::Job<QByteArray> SourceWriteBack::replay(const ApplicationDomain::Mail &, Sink::Operation, const QByteArray &, const QList<QByteArray> &)
{
    return KAsync::null<QByteArray>();
}

KAsync::Job<QByteArray> SourceWriteBack::replay(const ApplicationDomain::Folder &, Sink::Operation, const QByteArray &, const QList<QByteArray> &)
{
    return KAsync::null<QByteArray>();
}
