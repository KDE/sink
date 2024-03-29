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

#include <QCoreApplication>

#include "definitions.h"
#include "commands.h"
#include "bufferutils.h"
#include "synchronizerstore.h"
#include "datastorequery.h"
#include "createentity_generated.h"
#include "modifyentity_generated.h"
#include "deleteentity_generated.h"
#include "flush_generated.h"
#include "notification_generated.h"
#include "utils.h"

using namespace Sink;

bool operator==(const Synchronizer::SyncRequest &left, const Synchronizer::SyncRequest &right)
{
    return left.flushType == right.flushType
        && left.requestId == right.requestId
        && left.requestType == right.requestType
        && left.options == right.options
        && left.query == right.query
        && left.applicableEntities == right.applicableEntities;
}

Synchronizer::Synchronizer(const Sink::ResourceContext &context)
    : ChangeReplay(context, {"synchronizer"}),
    mLogCtx{"synchronizer"},
    mResourceContext(context),
    mEntityStore(Storage::EntityStore::Ptr::create(mResourceContext, mLogCtx)),
    mSyncStorage(Sink::storageLocation(), mResourceContext.instanceId() + ".synchronization", Sink::Storage::DataStore::DataStore::ReadWrite),
    mSyncInProgress(false),
    mAbort(false)
{
    mCurrentState.push(ApplicationDomain::Status::NoStatus);
    SinkTraceCtx(mLogCtx) << "Starting synchronizer: " << mResourceContext.resourceType << mResourceContext.instanceId();
}

Synchronizer::~Synchronizer()
{

}

void Synchronizer::setSecret(const QString &s)
{
    mSecret = s;

    if (!mSyncRequestQueue.isEmpty()) {
        processSyncQueue().exec();
    }
}

QString Synchronizer::secret() const
{
    return mSecret;
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
    Q_ASSERT(mEntityStore->hasTransaction());
    return *mEntityStore;
}

SynchronizerStore &Synchronizer::syncStore()
{
    if (!mSyncStore) {
        mSyncStore = QSharedPointer<SynchronizerStore>::create(syncTransaction());
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
    auto entityId = fbb.CreateString(sinkId.toStdString());
    auto type = fbb.CreateString(bufferType.toStdString());
    auto delta = Sink::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
    auto location = Sink::Commands::CreateCreateEntity(fbb, entityId, type, delta, replayToSource);
    Sink::Commands::FinishCreateEntityBuffer(fbb, location);
    enqueueCommand(Sink::Commands::CreateEntityCommand, BufferUtils::extractBuffer(fbb));
}

void Synchronizer::modifyEntity(const QByteArray &sinkId, qint64 revision, const QByteArray &bufferType, const Sink::ApplicationDomain::ApplicationDomainType &domainObject, const QByteArray &newResource, bool remove)
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
    auto type = fbb.CreateString(bufferType.toStdString());
    auto delta = Sink::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
    auto resource = newResource.isEmpty() ? 0 : fbb.CreateString(newResource.constData());
    auto location = Sink::Commands::CreateModifyEntity(fbb, revision, entityId, deletions, type, delta, replayToSource, modifiedProperties, resource, remove);
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

int Synchronizer::scanForRemovals(const QByteArray &bufferType, const std::function<void(const std::function<void(const QByteArray &key)> &callback)> &entryGenerator, std::function<bool(const QByteArray &remoteId)> exists)
{
    int count = 0;
    entryGenerator([this, bufferType, &exists, &count](const QByteArray &sinkId) {
        const auto remoteId = syncStore().resolveLocalId(bufferType, sinkId);
        SinkTraceCtx(mLogCtx) << "Checking for removal " << sinkId << remoteId;
        // If we have no remoteId, the entity hasn't been replayed to the source yet
        if (!remoteId.isEmpty()) {
            if (!exists(remoteId)) {
                SinkTraceCtx(mLogCtx) << "Found a removed entity: " << sinkId;
                count++;
                deleteEntity(sinkId, mEntityStore->maxRevision(), bufferType);
            }
        }
    });
    return count;
}

int Synchronizer::scanForRemovals(const QByteArray &bufferType, std::function<bool(const QByteArray &remoteId)> exists)
{
    return scanForRemovals(bufferType,
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
        const bool changed = [&] {
            for (const auto &property : entity.changedProperties()) {
                if (entity.getProperty(property) != current.getProperty(property)) {
                    SinkTraceCtx(mLogCtx) << "Property changed " << sinkId << property;
                    return true;
                }
            }
            return false;
        }();
        if (changed) {
            SinkTraceCtx(mLogCtx) << "Found a modified entity: " << sinkId;
            modifyEntity(sinkId, store.maxRevision(), bufferType, entity);
        } else {
            SinkTraceCtx(mLogCtx) << "Entity was not modified: " << sinkId;
        }
    });
}

void Synchronizer::modify(const QByteArray &bufferType, const QByteArray &remoteId, const Sink::ApplicationDomain::ApplicationDomainType &entity)
{
    const auto sinkId = syncStore().resolveRemoteId(bufferType, remoteId, false);
    if (sinkId.isEmpty()) {
        SinkWarningCtx(mLogCtx) << "Failed to find the local id for " << remoteId;
        return;
    }
    Storage::EntityStore store(mResourceContext, mLogCtx);
    modifyIfChanged(store, bufferType, sinkId, entity);
}

void Synchronizer::createOrModify(const QByteArray &bufferType, const QByteArray &remoteId, const Sink::ApplicationDomain::ApplicationDomainType &entity)
{
    SinkTraceCtx(mLogCtx) << "Create or modify" << bufferType << remoteId;
    const auto sinkId = syncStore().resolveRemoteId(bufferType, remoteId);
    if (sinkId.isEmpty()) {
        SinkWarningCtx(mLogCtx) << "Failed to create a local id for " << remoteId;
        Q_ASSERT(false);
        return;
    }
    Storage::EntityStore store(mResourceContext, mLogCtx);
    if (!store.contains(bufferType, sinkId)) {
        SinkTraceCtx(mLogCtx) << "Found a new entity: " << remoteId;
        createEntity(sinkId, bufferType, entity);
    } else { // modification
        modifyIfChanged(store, bufferType, sinkId, entity);
    }
}

template<typename DomainType>
void Synchronizer::createOrModify(const QByteArray &bufferType, const QByteArray &remoteId, const DomainType &entity, const QHash<QByteArray, Sink::Query::Comparator> &mergeCriteria)
{
    SinkTraceCtx(mLogCtx) << "Create or modify" << bufferType << remoteId;
    const auto sinkId = syncStore().resolveRemoteId(bufferType, remoteId);
    if (sinkId.isEmpty()) {
        SinkWarningCtx(mLogCtx) << "Failed to create a local id for " << remoteId;
        Q_ASSERT(false);
        return;
    }
    Storage::EntityStore store(mResourceContext, mLogCtx);
    if (!store.contains(bufferType, sinkId)) {
        if (!mergeCriteria.isEmpty()) {
            Sink::Query query;
            for (auto it = mergeCriteria.constBegin(); it != mergeCriteria.constEnd(); it++) {
                query.filter(it.key(), it.value());
            }
            bool merge = false;
            DataStoreQuery dataStoreQuery{query, ApplicationDomain::getTypeName<DomainType>(), store};
            auto resultSet = dataStoreQuery.execute();
            resultSet.replaySet(0, 1, [this, &merge, bufferType, remoteId](const ResultSet::Result &r) {
                merge = true;
                SinkTraceCtx(mLogCtx) << "Merging local entity with remote entity: " << r.entity.identifier() << remoteId;
                syncStore().recordRemoteId(bufferType, r.entity.identifier(), remoteId);
            });

            if (!merge) {
                SinkTraceCtx(mLogCtx) << "Found a new entity: " << remoteId;
                createEntity(sinkId, bufferType, entity);
            }
        } else {
            SinkTraceCtx(mLogCtx) << "Found a new entity: " << remoteId;
            createEntity(sinkId, bufferType, entity);
        }
    } else { // modification
        modifyIfChanged(store, bufferType, sinkId, entity);
    }
}

QByteArrayList Synchronizer::resolveQuery(const QueryBase &query)
{
    if (query.type().isEmpty()) {
        SinkWarningCtx(mLogCtx) << "Can't resolve a query without a type" << query;
        return {};
    }
    QByteArrayList result;
    Storage::EntityStore store{mResourceContext, mLogCtx};
    DataStoreQuery dataStoreQuery{query, query.type(), store};
    auto resultSet = dataStoreQuery.execute();
    resultSet.replaySet(0, 0, [&](const ResultSet::Result &r) {
        result << r.entity.identifier();
    });
    return result;
}

QByteArrayList Synchronizer::resolveFilter(const QueryBase::Comparator &filter)
{
    if (filter.value.canConvert<QByteArray>()) {
        const auto value = filter.value.value<QByteArray>();
        if (value.isEmpty()) {
            SinkErrorCtx(mLogCtx) << "Tried to filter for an empty value: " << filter;
        } else {
            return {filter.value.value<QByteArray>()};
        }
    } else if (filter.value.canConvert<QueryBase>()) {
        return resolveQuery(filter.value.value<QueryBase>());
    } else if (filter.value.canConvert<Query>()) {
        return resolveQuery(filter.value.value<Query>());
    } else if (filter.value.canConvert<SyncScope>()) {
        return resolveQuery(filter.value.value<SyncScope>());
    } else {
        SinkWarningCtx(mLogCtx) << "unknown filter type: " << filter;
        Q_ASSERT(false);
    }
    return {};
}

template<typename DomainType>
void Synchronizer::modify(const DomainType &entity, const QByteArray &newResource, bool remove)
{
    modifyEntity(entity.identifier(), entity.revision(), ApplicationDomain::getTypeName<DomainType>(), entity, newResource, remove);
}

QList<Synchronizer::SyncRequest> Synchronizer::getSyncRequests(const Sink::QueryBase &query)
{
    return {Synchronizer::SyncRequest{query, "sync"}};
}

void Synchronizer::mergeIntoQueue(const Synchronizer::SyncRequest &request, QList<Synchronizer::SyncRequest> &queue)
{
    queue << request;
}

void Synchronizer::addToQueue(const Synchronizer::SyncRequest &request)
{
    mergeIntoQueue(request, mSyncRequestQueue);
}

void Synchronizer::synchronize(const Sink::QueryBase &query)
{
    SinkTraceCtx(mLogCtx) << "Synchronizing" << query;
    auto newRequests = getSyncRequests(query);
    for (const auto &request: newRequests) {
        auto shouldSkip = [&] {
            for (auto &r : mSyncRequestQueue) {
                if (r == request) {
                    //Merge
                    SinkTraceCtx(mLogCtx) << "Merging equal request " << request.query << "\n to" << r.query;
                    return true;
                }
            }
            return false;
        };

        if (shouldSkip()) {
            continue;
        }
        mergeIntoQueue(request, mSyncRequestQueue);
    }
    processSyncQueue().exec();
}

void Synchronizer::clearQueue()
{
    //Complete all pending flushes. Without this pending flushes would get stuck indefinitely when we clear the queue on failure.
    //TODO we should probably fail them instead
    for (const auto &request : mSyncRequestQueue) {
        if (request.requestType == Synchronizer::SyncRequest::Flush) {
            SinkTraceCtx(mLogCtx) << "Emitting flush completion: " << request.requestId;
            emitNotification(Notification::FlushCompletion, 0, "", request.requestId);
        }
    }
    mSyncRequestQueue.clear();
}

void Synchronizer::abort()
{
    SinkLogCtx(mLogCtx) << "Aborting all running synchronization requests";
    clearQueue();
    mAbort = true;
}

void Synchronizer::flush(int commandId, const QByteArray &flushId)
{
    Q_ASSERT(!flushId.isEmpty());
    SinkTraceCtx(mLogCtx) << "Flushing the synchronization queue " << flushId;
    mSyncRequestQueue << Synchronizer::SyncRequest{Synchronizer::SyncRequest::Flush, commandId, flushId};
    processSyncQueue().exec();
}

void Synchronizer::flushComplete(const QByteArray &flushId)
{
    SinkTraceCtx(mLogCtx) << "Flush complete: " << flushId;
    if (mPendingSyncRequests.contains(flushId)) {
        const auto requests = mPendingSyncRequests.values(flushId);
        for (const auto &r : requests) {
            //We want to process the pending request before any others in the queue
            mSyncRequestQueue.prepend(r);
        }
        mPendingSyncRequests.remove(flushId);
        processSyncQueue().exec();
    }
}

void Synchronizer::emitNotification(Notification::NoticationType type, int code, const QString &message, const QByteArray &id, const QByteArray &applicableEntitiesType, const QByteArrayList &entities)
{
    Sink::Notification n;
    n.id = id;
    n.type = type;
    n.message = message;
    n.code = code;
    n.entitiesType = applicableEntitiesType;
    n.entities = entities;
    emit notify(n);
}

void Synchronizer::emitProgressNotification(Notification::NoticationType type, int progress, int total, const QByteArray &id, const QByteArray &entitiesType, const QByteArrayList &entities)
{
    Sink::Notification n;
    n.id = id;
    n.type = type;
    n.progress = progress;
    n.total = total;
    n.entitiesType = entitiesType;
    n.entities = entities;
    emit notify(n);
}

void Synchronizer::reportProgress(int progress, int total, const QByteArrayList &entities)
{
    if (progress > 0 && total > 0) {
        //Limit progress updates for large amounts
        if (total >= 1000 && progress % 100 != 0) {
            return;
        } else if (total >= 100 && progress % 10 != 0) {
            return;
        }
        SinkLogCtx(mLogCtx) << "Progress: " << progress << " out of " << total << mCurrentRequest.requestId << mCurrentRequest.applicableEntities;
        const auto applicableEntities = [&] {
            if (entities.isEmpty()) {
                return mCurrentRequest.applicableEntities;
            }
            return entities;
        }();
        emitProgressNotification(Notification::Progress, progress, total, mCurrentRequest.requestId, mCurrentRequest.query.type(), applicableEntities);
    }
}

void Synchronizer::setStatusFromResult(const KAsync::Error &error, const QString &s, const QByteArray &requestId)
{
    if (error) {
        if (error.errorCode == ApplicationDomain::ConnectionError) {
            //Couldn't connect, so we assume we don't have a network connection.
            setStatus(ApplicationDomain::OfflineStatus, s, requestId);
        } else if (error.errorCode == ApplicationDomain::NoServerError) {
            //Failed to contact the server.
            setStatus(ApplicationDomain::OfflineStatus, s, requestId);
        } else if (error.errorCode == ApplicationDomain::ConfigurationError) {
            //There is an error with the configuration.
            setStatus(ApplicationDomain::ErrorStatus, s, requestId);
        } else if (error.errorCode == ApplicationDomain::LoginError) {
            //If we failed to login altough we could connect that indicates a problem with our setup.
            setStatus(ApplicationDomain::ErrorStatus, s, requestId);
        } else if (error.errorCode == ApplicationDomain::ConnectionLostError) {
            //We've lost the connection so we assume the connection to the server broke.
            setStatus(ApplicationDomain::OfflineStatus, s, requestId);
        }
        //We don't know what kind of error this was, so we assume it's transient and don't change our status.
    } else {
        //An operation against the server worked, so we're probably online.
        setStatus(ApplicationDomain::ConnectedStatus, s, requestId);
    }
}

KAsync::Job<void> Synchronizer::processRequest(const SyncRequest &request)
{
    if (request.options & SyncRequest::RequestFlush) {
        return KAsync::start([=] {
            //Trigger a flush and record original request without flush option
            auto modifiedRequest = request;
            modifiedRequest.options = SyncRequest::NoOptions;
            //Normally we won't have a requestId here
            if (modifiedRequest.requestId.isEmpty()) {
                modifiedRequest.requestId = createUuid();
            }
            SinkTraceCtx(mLogCtx) << "Enqueuing flush request " << modifiedRequest.requestId;

            //The sync request will be executed once the flush has completed
            mPendingSyncRequests.insert(modifiedRequest.requestId, modifiedRequest);

            flatbuffers::FlatBufferBuilder fbb;
            auto flushId = fbb.CreateString(modifiedRequest.requestId.toStdString());
            auto location = Sink::Commands::CreateFlush(fbb, flushId, static_cast<int>(Sink::Flush::FlushSynchronization));
            Sink::Commands::FinishFlushBuffer(fbb, location);
            enqueueCommand(Sink::Commands::FlushCommand, BufferUtils::extractBuffer(fbb));
        });
    } else if (request.requestType == Synchronizer::SyncRequest::Synchronization) {
        return KAsync::start([this, request] {
            SinkLogCtx(mLogCtx) << "Synchronizing:" << request.query;
            setBusy(true, "Synchronization has started.", request.requestId);
            emitNotification(Notification::Info, ApplicationDomain::SyncInProgress, {}, {}, request.applicableEntitiesType, request.applicableEntities);
        }).then(synchronizeWithSource(request.query)).then([this] {
            //Commit after every request, so implementations only have to commit more if they add a lot of data.
            commit();
        }).then<void>([this, request](const KAsync::Error &error) {
            setStatusFromResult(error, "Synchronization has ended.", request.requestId);
            if (error) {
                //Emit notification with error
                SinkWarningCtx(mLogCtx) << "Synchronization failed: " << error;
                emitNotification(Notification::Warning, ApplicationDomain::SyncError, {}, {}, request.applicableEntitiesType, request.applicableEntities);
                return KAsync::error(error);
            } else {
                SinkLogCtx(mLogCtx) << "Done Synchronizing";
                emitNotification(Notification::Info, ApplicationDomain::SyncSuccess, {}, {}, request.applicableEntitiesType, request.applicableEntities);
                return KAsync::null();
            }
        });
    } else if (request.requestType == Synchronizer::SyncRequest::Flush) {
        return KAsync::start([=] {
            Q_ASSERT(!request.requestId.isEmpty());
            //FIXME it looks like this is emitted before the replay actually finishes
            if (request.flushType == Flush::FlushReplayQueue) {
                SinkTraceCtx(mLogCtx) << "Emitting flush completion: " << request.requestId;
                emitNotification(Notification::FlushCompletion, 0, "", request.requestId);
            } else {
                flatbuffers::FlatBufferBuilder fbb;
                auto flushId = fbb.CreateString(request.requestId.toStdString());
                auto location = Sink::Commands::CreateFlush(fbb, flushId, static_cast<int>(Sink::Flush::FlushSynchronization));
                Sink::Commands::FinishFlushBuffer(fbb, location);
                enqueueCommand(Sink::Commands::FlushCommand, BufferUtils::extractBuffer(fbb));
            }
        });
    } else if (request.requestType == Synchronizer::SyncRequest::ChangeReplay) {
        if (ChangeReplay::allChangesReplayed()) {
            return KAsync::null();
        } else {
            return KAsync::start([this, request] {
                setBusy(true, "ChangeReplay has started.", request.requestId);
                SinkLogCtx(mLogCtx) << "Replaying changes.";
            })
            .then(replayNextRevision())
            .then<void>([this, request](const KAsync::Error &error) {
                setStatusFromResult(error, "Changereplay has ended.", request.requestId);
                if (error) {
                    SinkWarningCtx(mLogCtx) << "Changereplay failed: " << error;
                    return KAsync::error(error);
                } else {
                    SinkLogCtx(mLogCtx) << "Done replaying changes";
                    return KAsync::null();
                }
            });
        }
    } else {
        SinkWarningCtx(mLogCtx) << "Unknown request type: " << request.requestType;
        return KAsync::error(KAsync::Error{"Unknown request type."});
    }

}

/*
 * We're using a stack so we can go back to whatever we had after the temporary busy status.
 * Whenever we do change the status we emit a status notification.
 */
void Synchronizer::setStatus(ApplicationDomain::Status state, const QString &reason, const QByteArray requestId)
{
    //We won't be able to execute any of the coming requests, so clear them
    if (state == ApplicationDomain::OfflineStatus || state == ApplicationDomain::ErrorStatus) {
        clearQueue();
    }
    if (state != mCurrentState.top()) {
        //The busy state is transient and we want to override it.
        if (mCurrentState.top() == ApplicationDomain::BusyStatus) {
            mCurrentState.pop();
        }
        if (state != mCurrentState.top()) {
            //Always leave the first state intact
            if (mCurrentState.count() > 1 && state != ApplicationDomain::BusyStatus) {
                mCurrentState.pop();
            }
            mCurrentState.push(state);
        }
        //We should never have more than: (NoStatus, $SOMESTATUS, BusyStatus)
        if (mCurrentState.count() > 3) {
            qWarning() << mCurrentState;
            Q_ASSERT(false);
        }
        emitNotification(Notification::Status, state, reason, requestId);
    }
}

void Synchronizer::resetStatus(const QByteArray requestId)
{
    mCurrentState.pop();
    emitNotification(Notification::Status, mCurrentState.top(), {}, requestId);
}

void Synchronizer::setBusy(bool busy, const QString &reason, const QByteArray requestId)
{
    if (busy) {
        setStatus(ApplicationDomain::BusyStatus, reason, requestId);
    } else {
        if (mCurrentState.top() == ApplicationDomain::BusyStatus) {
            resetStatus(requestId);
        }
    }
}

KAsync::Job<void> Synchronizer::processSyncQueue()
{
    if (secret().isEmpty()) {
        SinkTraceCtx(mLogCtx) << "Secret not available but required.";
        emitNotification(Notification::Warning, ApplicationDomain::SyncError, "Secret is not available.", {}, {});
        return KAsync::null<void>();
    }
    if (mSyncRequestQueue.isEmpty()) {
        SinkLogCtx(mLogCtx) << "All requests processed.";
        return KAsync::null<void>();
    }
    if (mSyncInProgress) {
        SinkTraceCtx(mLogCtx) << "Sync still in progress.";
        return KAsync::null<void>();
    }
    //Don't process any new requests until we're done with the pending ones.
    //Otherwise we might process a flush before the previous request actually completed.
    if (!mPendingSyncRequests.isEmpty()) {
        SinkTraceCtx(mLogCtx) << "We still have pending sync requests. Not executing next request.";
        return KAsync::null<void>();
    }

    const auto request = mSyncRequestQueue.takeFirst();
    return KAsync::start([=] {
        SinkTraceCtx(mLogCtx) << "Start processing request " << request.requestType;
        mTime.start();
        mMessageQueue->startTransaction();
        mEntityStore->startTransaction(Sink::Storage::DataStore::ReadOnly);
        mSyncInProgress = true;
        mCurrentRequest = request;
    })
    .then(processRequest(request))
    .then<void>([this, request](const KAsync::Error &error) {
        SinkTraceCtx(mLogCtx) << "Sync request processed " << Sink::Log::TraceTime(mTime.elapsed());
        setBusy(false, {}, request.requestId);
        mCurrentRequest = {};
        mEntityStore->abortTransaction();
        mSyncTransaction.abort();
        mMessageQueue->commit();
        mSyncStore.clear();
        mSyncInProgress = false;
        mAbort = false;
        if (allChangesReplayed()) {
            emit changesReplayed();
        }
        if (error) {
            SinkWarningCtx(mLogCtx) << "Error during sync: " << error;
            emitNotification(Notification::Error, error.errorCode, error.errorMessage, request.requestId);
        }
        //In case we got more requests meanwhile.
        return processSyncQueue();
    });
}

bool Synchronizer::aborting() const
{
    return mAbort;
}

void Synchronizer::commit()
{
    SinkTraceCtx(mLogCtx) << "Commit." << Sink::Log::TraceTime(mTime.elapsed());
    mMessageQueue->commit();
    mSyncTransaction.commit();
    mSyncStore.clear();

    //Avoid accumulating free pages at the cost of not executing a full sync on a consistent db view
    if (mEntityStore->hasTransaction()) {
        mEntityStore->abortTransaction();
        mEntityStore->startTransaction(Sink::Storage::DataStore::ReadOnly);
    }

    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    if (mSyncInProgress) {
        mMessageQueue->startTransaction();
    }
}

Sink::Storage::DataStore::DataStore::Transaction &Synchronizer::syncTransaction()
{
    if (!mSyncTransaction) {
        SinkTraceCtx(mLogCtx) << "Starting transaction on sync store.";
        mSyncTransaction = mSyncStorage.createTransaction(Sink::Storage::DataStore::DataStore::ReadWrite);
    }
    return mSyncTransaction;
}

void Synchronizer::revisionChanged()
{
    //One replay request is enough
    for (const auto &r : mSyncRequestQueue) {
        if (r.requestType == Synchronizer::SyncRequest::ChangeReplay) {
            return;
        }
    }
    mSyncRequestQueue << Synchronizer::SyncRequest{Synchronizer::SyncRequest::ChangeReplay, "changereplay"};
    processSyncQueue().exec();
}

bool Synchronizer::canReplay(const QByteArray &type, const QByteArray &key, const QByteArray &value)
{
    Sink::EntityBuffer buffer(value);
    const Sink::Entity &entity = buffer.entity();
    const auto metadataBuffer = Sink::EntityBuffer::readBuffer<Sink::Metadata>(entity.metadata());
    Q_ASSERT(metadataBuffer);
    if (!metadataBuffer->replayToSource()) {
        SinkTraceCtx(mLogCtx) << "Change is coming from the source";
    }
    return metadataBuffer->replayToSource();
}

KAsync::Job<void> Synchronizer::replay(const QByteArray &type, const QByteArray &key, const QByteArray &value)
{
    SinkTraceCtx(mLogCtx) << "Replaying" << type << key;

    Sink::EntityBuffer buffer(value);
    const Sink::Entity &entity = buffer.entity();
    const auto metadataBuffer = Sink::EntityBuffer::readBuffer<Sink::Metadata>(entity.metadata());
    if (!metadataBuffer) {
        SinkErrorCtx(mLogCtx) << "No metadata buffer available.";
        return KAsync::error("No metadata buffer");
    }
    if (mSyncTransaction) {
        SinkErrorCtx(mLogCtx) << "Leftover sync transaction.";
        mSyncTransaction.abort();
    }
    if (mSyncStore) {
        SinkErrorCtx(mLogCtx) << "Leftover sync store.";
        mSyncStore.clear();
    }
    Q_ASSERT(metadataBuffer);
    Q_ASSERT(!mSyncStore);
    Q_ASSERT(!mSyncTransaction);
    //The entitystore transaction is handled by processSyncQueue
    Q_ASSERT(mEntityStore->hasTransaction());

    const auto operation = metadataBuffer->operation();
    // TODO: should not use internal representations
    const auto uid = Sink::Storage::Key::fromDisplayByteArray(key).identifier().toDisplayByteArray();
    const auto modifiedProperties = metadataBuffer->modifiedProperties() ? BufferUtils::fromVector(*metadataBuffer->modifiedProperties()) : QByteArrayList();
    QByteArray oldRemoteId;

    if (operation != Sink::Operation_Creation) {
        oldRemoteId = syncStore().resolveLocalId(type, uid);
        //oldRemoteId can be empty if the resource implementation didn't return a remoteid
    }
    SinkLogCtx(mLogCtx) << "Replaying: " << key << "Type: " << type << "Uid: " << uid << "Rid: " << oldRemoteId << "Revision: " << metadataBuffer->revision() << "Modified properties" << modifiedProperties;

    //If the entity has been removed already and this is not the removal, skip over.
    //This is important so we can unblock changereplay by removing entities.
    bool skipOver = false;
    store().readLatest(type, uid, [&](const ApplicationDomain::ApplicationDomainType &, Sink::Operation latestOperation) {
        if (latestOperation == Sink::Operation_Removal && operation != Sink::Operation_Removal) {
            skipOver = true;
        }
    });
    if (skipOver) {
        SinkLogCtx(mLogCtx) << "Skipping over already removed entity";
        return KAsync::null();
    }

    KAsync::Job<QByteArray> job = KAsync::null<QByteArray>();
    //TODO This requires supporting every domain type here as well. Can we solve this better so we can do the dispatch somewhere centrally?
    if (type == ApplicationDomain::getTypeName<ApplicationDomain::Folder>()) {
        job = replay(store().readEntity<ApplicationDomain::Folder>(key), operation, oldRemoteId, modifiedProperties);
    } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Mail>()) {
        job = replay(store().readEntity<ApplicationDomain::Mail>(key), operation, oldRemoteId, modifiedProperties);
    } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Contact>()) {
        job = replay(store().readEntity<ApplicationDomain::Contact>(key), operation, oldRemoteId, modifiedProperties);
    } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Addressbook>()) {
        job = replay(store().readEntity<ApplicationDomain::Addressbook>(key), operation, oldRemoteId, modifiedProperties);
    } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Event>()) {
        job = replay(store().readEntity<ApplicationDomain::Event>(key), operation, oldRemoteId, modifiedProperties);
    } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Todo>()) {
        job = replay(store().readEntity<ApplicationDomain::Todo>(key), operation, oldRemoteId, modifiedProperties);
    } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Calendar>()) {
        job = replay(store().readEntity<ApplicationDomain::Calendar>(key), operation, oldRemoteId, modifiedProperties);
    } else {
        SinkErrorCtx(mLogCtx) << "Replayed unknown type: " << type;
    }

    return job.then([=](const KAsync::Error &error, const QByteArray &remoteId) {

        //Returning an error here means we stop replaying, so we only to that for known-to-be-transient errors.
        if (error) {
            switch (error.errorCode) {
                case ApplicationDomain::ConnectionError:
                case ApplicationDomain::NoServerError:
                case ApplicationDomain::ConfigurationError:
                case ApplicationDomain::LoginError:
                case ApplicationDomain::ConnectionLostError:
                    SinkTraceCtx(mLogCtx) << "Error during changereplay (aborting):" << error;
                    return KAsync::error(error);
                default:
                    SinkErrorCtx(mLogCtx) << "Error during changereplay (continuing):" << error;
                    break;

            }
        }

        switch (operation) {
            case Sink::Operation_Creation: {
                SinkTraceCtx(mLogCtx) << "Replayed creation with remote id: " << remoteId;
                if (!remoteId.isEmpty()) {
                    syncStore().recordRemoteId(type, uid, remoteId);
                }
            }
            break;
            case Sink::Operation_Modification: {
                SinkTraceCtx(mLogCtx) << "Replayed modification with remote id: " << remoteId;
                if (!remoteId.isEmpty()) {
                    syncStore().updateRemoteId(type, uid, remoteId);
                }
            }
            break;
            case Sink::Operation_Removal: {
                SinkTraceCtx(mLogCtx) << "Replayed removal with remote id: " << oldRemoteId;
                if (!oldRemoteId.isEmpty()) {
                    syncStore().removeRemoteId(type, uid, oldRemoteId);
                }
            }
            break;
            default:
                SinkErrorCtx(mLogCtx) << "Unkown operation" << operation;
        }

        //We need to commit here otherwise the next change-replay step will abort the transaction
        mSyncStore.clear();
        mSyncTransaction.commit();

        //Ignore errors if not caught above
        return KAsync::null();
    });
}

void Synchronizer::notReplaying(const QByteArray &type, const QByteArray &key, const QByteArray &value)
{

    Sink::EntityBuffer buffer(value);
    const Sink::Entity &entity = buffer.entity();
    const auto metadataBuffer = Sink::EntityBuffer::readBuffer<Sink::Metadata>(entity.metadata());
    if (!metadataBuffer) {
        SinkErrorCtx(mLogCtx) << "No metadata buffer available.";
        Q_ASSERT(false);
        return;
    }
    if (metadataBuffer->operation() == Sink::Operation_Removal) {
        const auto uid = Sink::Storage::Key::fromDisplayByteArray(key).identifier().toDisplayByteArray();
        const auto oldRemoteId = syncStore().resolveLocalId(type, uid);
        SinkLogCtx(mLogCtx) << "Cleaning up removal with remote id: " << oldRemoteId;
        if (!oldRemoteId.isEmpty()) {
            syncStore().removeRemoteId(type, uid, oldRemoteId);
        }
    }
    mSyncStore.clear();
    mSyncTransaction.commit();
}

KAsync::Job<QByteArray> Synchronizer::replay(const ApplicationDomain::Contact &, Sink::Operation, const QByteArray &, const QList<QByteArray> &)
{
    return KAsync::null<QByteArray>();
}

KAsync::Job<QByteArray> Synchronizer::replay(const ApplicationDomain::Addressbook &, Sink::Operation, const QByteArray &, const QList<QByteArray> &)
{
    return KAsync::null<QByteArray>();
}

KAsync::Job<QByteArray> Synchronizer::replay(const ApplicationDomain::Mail &, Sink::Operation, const QByteArray &, const QList<QByteArray> &)
{
    return KAsync::null<QByteArray>();
}

KAsync::Job<QByteArray> Synchronizer::replay(const ApplicationDomain::Folder &, Sink::Operation, const QByteArray &, const QList<QByteArray> &)
{
    return KAsync::null<QByteArray>();
}

KAsync::Job<QByteArray> Synchronizer::replay(const ApplicationDomain::Event &, Sink::Operation, const QByteArray &, const QList<QByteArray> &)
{
    return KAsync::null<QByteArray>();
}

KAsync::Job<QByteArray> Synchronizer::replay(const ApplicationDomain::Todo &, Sink::Operation, const QByteArray &, const QList<QByteArray> &)
{
    return KAsync::null<QByteArray>();
}

KAsync::Job<QByteArray> Synchronizer::replay(const ApplicationDomain::Calendar &, Sink::Operation, const QByteArray &, const QList<QByteArray> &)
{
    return KAsync::null<QByteArray>();
}

bool Synchronizer::allChangesReplayed()
{
    if (!mSyncRequestQueue.isEmpty()) {
        SinkTraceCtx(mLogCtx) << "Queue is not empty";
        return false;
    }
    return ChangeReplay::allChangesReplayed();
}

#define REGISTER_TYPE(T)                                                          \
    template void Synchronizer::createOrModify(const QByteArray &bufferType, const QByteArray &remoteId, const T &entity, const QHash<QByteArray, Sink::Query::Comparator> &mergeCriteria); \
    template void Synchronizer::modify(const T &entity, const QByteArray &newResource, bool remove);

SINK_REGISTER_TYPES()

