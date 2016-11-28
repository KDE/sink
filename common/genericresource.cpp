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
#include "genericresource.h"

#include "entitybuffer.h"
#include "pipeline.h"
#include "queuedcommand_generated.h"
#include "createentity_generated.h"
#include "modifyentity_generated.h"
#include "deleteentity_generated.h"
#include "inspection_generated.h"
#include "notification_generated.h"
#include "flush_generated.h"
#include "domainadaptor.h"
#include "commands.h"
#include "index.h"
#include "log.h"
#include "definitions.h"
#include "bufferutils.h"
#include "adaptorfactoryregistry.h"
#include "synchronizer.h"
#include "commandprocessor.h"

#include <QUuid>
#include <QDataStream>
#include <QTime>

static int sBatchSize = 100;
// This interval directly affects the roundtrip time of single commands
static int sCommitInterval = 10;

using namespace Sink;
using namespace Sink::Storage;

GenericResource::GenericResource(const ResourceContext &resourceContext, const QSharedPointer<Pipeline> &pipeline )
    : Sink::Resource(),
      mResourceContext(resourceContext),
      mUserQueue(Sink::storageLocation(), resourceContext.instanceId() + ".userqueue"),
      mSynchronizerQueue(Sink::storageLocation(), resourceContext.instanceId() + ".synchronizerqueue"),
      mPipeline(pipeline ? pipeline : QSharedPointer<Sink::Pipeline>::create(resourceContext)),
      mError(0),
      mClientLowerBoundRevision(std::numeric_limits<qint64>::max())
{
    mProcessor = std::unique_ptr<CommandProcessor>(new CommandProcessor(mPipeline.data(), QList<MessageQueue *>() << &mUserQueue << &mSynchronizerQueue));
    mProcessor->setFlushCommand([this](void const *command, size_t size) {
        flatbuffers::Verifier verifier((const uint8_t *)command, size);
        if (Sink::Commands::VerifyFlushBuffer(verifier)) {
            auto buffer = Sink::Commands::GetFlush(command);
            const auto flushType = buffer->type();
            const auto flushId = BufferUtils::extractBuffer(buffer->id());
            if (flushType == Sink::Flush::FlushReplayQueue) {
                SinkTrace() << "Flushing synchronizer ";
                mSynchronizer->flush(flushType, flushId);
            } else {
                SinkTrace() << "Emitting flush completion" << flushId;
                Sink::Notification n;
                n.type = Sink::Notification::FlushCompletion;
                n.id = flushId;
                emit notify(n);
            }
            return KAsync::null<void>();
        }
        return KAsync::error<void>(-1, "Invalid flush command.");
    });
    QObject::connect(mProcessor.get(), &CommandProcessor::error, [this](int errorCode, const QString &msg) { onProcessorError(errorCode, msg); });
    QObject::connect(mProcessor.get(), &CommandProcessor::notify, this, &GenericResource::notify);
    QObject::connect(mPipeline.data(), &Pipeline::revisionUpdated, this, &Resource::revisionUpdated);

    mCommitQueueTimer.setInterval(sCommitInterval);
    mCommitQueueTimer.setSingleShot(true);
    QObject::connect(&mCommitQueueTimer, &QTimer::timeout, &mUserQueue, &MessageQueue::commit);
}

GenericResource::~GenericResource()
{
}

void GenericResource::setupPreprocessors(const QByteArray &type, const QVector<Sink::Preprocessor *> &preprocessors)
{
    mPipeline->setPreprocessors(type, preprocessors);
}

void GenericResource::setupSynchronizer(const QSharedPointer<Synchronizer> &synchronizer)
{
    mSynchronizer = synchronizer;
    mSynchronizer->setup([this](int commandId, const QByteArray &data) {
        enqueueCommand(mSynchronizerQueue, commandId, data);
    }, mSynchronizerQueue);
    {
        auto ret = QObject::connect(mSynchronizer.data(), &Synchronizer::replayingChanges, [this]() {
            Sink::Notification n;
            n.id = "changereplay";
            n.type = Sink::Notification::Status;
            n.message = "Replaying changes.";
            n.code = Sink::ApplicationDomain::BusyStatus;
            emit notify(n);
        });
        Q_ASSERT(ret);
    }
    {
        auto ret = QObject::connect(mSynchronizer.data(), &Synchronizer::changesReplayed, [this]() {
            Sink::Notification n;
            n.id = "changereplay";
            n.type = Sink::Notification::Status;
            n.message = "All changes have been replayed.";
            n.code = Sink::ApplicationDomain::ConnectedStatus;
            emit notify(n);
        });
        Q_ASSERT(ret);
    }

    mProcessor->setOldestUsedRevision(mSynchronizer->getLastReplayedRevision());
    QObject::connect(mPipeline.data(), &Pipeline::revisionUpdated, mSynchronizer.data(), &ChangeReplay::revisionChanged, Qt::QueuedConnection);
    QObject::connect(mSynchronizer.data(), &ChangeReplay::changesReplayed, this, &GenericResource::updateLowerBoundRevision);
    QMetaObject::invokeMethod(mSynchronizer.data(), "revisionChanged", Qt::QueuedConnection);
}

void GenericResource::setupInspector(const QSharedPointer<Inspector> &inspector)
{
    mProcessor->setInspector(inspector);
}

void GenericResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier, Sink::Storage::DataStore::ReadWrite).removeFromDisk();
    Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".userqueue", Sink::Storage::DataStore::ReadWrite).removeFromDisk();
    Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".synchronizerqueue", Sink::Storage::DataStore::ReadWrite).removeFromDisk();
    Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".changereplay", Sink::Storage::DataStore::ReadWrite).removeFromDisk();
    Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".synchronization", Sink::Storage::DataStore::ReadWrite).removeFromDisk();
}

qint64 GenericResource::diskUsage(const QByteArray &instanceIdentifier)
{
    auto size = Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier, Sink::Storage::DataStore::ReadOnly).diskUsage();
    size += Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".userqueue", Sink::Storage::DataStore::ReadOnly).diskUsage();
    size += Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".synchronizerqueue", Sink::Storage::DataStore::ReadOnly).diskUsage();
    size += Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".changereplay", Sink::Storage::DataStore::ReadOnly).diskUsage();
    return size;
}

void GenericResource::onProcessorError(int errorCode, const QString &errorMessage)
{
    SinkWarning() << "Received error from Processor: " << errorCode << errorMessage;
    mError = errorCode;
}

int GenericResource::error() const
{
    return mError;
}

void GenericResource::enqueueCommand(MessageQueue &mq, int commandId, const QByteArray &data)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto commandData = Sink::EntityBuffer::appendAsVector(fbb, data.constData(), data.size());
    auto buffer = Sink::CreateQueuedCommand(fbb, commandId, commandData);
    Sink::FinishQueuedCommandBuffer(fbb, buffer);
    mq.enqueue(fbb.GetBufferPointer(), fbb.GetSize());
}

void GenericResource::processCommand(int commandId, const QByteArray &data)
{
    if (commandId == Commands::FlushCommand) {
        processFlushCommand(data);
        return;
    }
    static int modifications = 0;
    mUserQueue.startTransaction();
    enqueueCommand(mUserQueue, commandId, data);
    modifications++;
    if (modifications >= sBatchSize) {
        mUserQueue.commit();
        modifications = 0;
        mCommitQueueTimer.stop();
    } else {
        mCommitQueueTimer.start();
    }
}

void GenericResource::processFlushCommand(const QByteArray &data)
{
    flatbuffers::Verifier verifier((const uint8_t *)data.constData(), data.size());
    if (Sink::Commands::VerifyFlushBuffer(verifier)) {
        auto buffer = Sink::Commands::GetFlush(data.constData());
        const auto flushType = buffer->type();
        const auto flushId = BufferUtils::extractBuffer(buffer->id());
        if (flushType == Sink::Flush::FlushSynchronization) {
            mSynchronizer->flush(flushType, flushId);
        } else {
            mUserQueue.startTransaction();
            enqueueCommand(mUserQueue, Commands::FlushCommand, data);
            mUserQueue.commit();
        }
    }

}

KAsync::Job<void> GenericResource::synchronizeWithSource(const Sink::QueryBase &query)
{
    return KAsync::start<void>([this, query] {

        Sink::Notification n;
        n.id = "sync";
        n.type = Sink::Notification::Status;
        n.message = "Synchronization has started.";
        n.code = Sink::ApplicationDomain::BusyStatus;
        emit notify(n);

        SinkLog() << " Synchronizing";
        return mSynchronizer->synchronize(query)
            .then<void>([this](const KAsync::Error &error) {
                if (!error) {
                    SinkLog() << "Done Synchronizing";
                    Sink::Notification n;
                    n.id = "sync";
                    n.type = Sink::Notification::Status;
                    n.message = "Synchronization has ended.";
                    n.code = Sink::ApplicationDomain::ConnectedStatus;
                    emit notify(n);
                    return KAsync::null();
                }
                return KAsync::error(error);
            });
    });
}

static void waitForDrained(KAsync::Future<void> &f, MessageQueue &queue)
{
    if (queue.isEmpty()) {
        f.setFinished();
    } else {
        QObject::connect(&queue, &MessageQueue::drained, [&f]() { f.setFinished(); });
    }
};

KAsync::Job<void> GenericResource::processAllMessages()
{
    // We have to wait for all items to be processed to ensure the synced items are available when a query gets executed.
    // TODO: report errors while processing sync?
    // TODO JOBAPI: A helper that waits for n events and then continues?
    return KAsync::start<void>([this](KAsync::Future<void> &f) {
               if (mCommitQueueTimer.isActive()) {
                   auto context = new QObject;
                   QObject::connect(&mCommitQueueTimer, &QTimer::timeout, context, [&f, context]() {
                       delete context;
                       f.setFinished();
                   });
               } else {
                   f.setFinished();
               }
           })
        .then<void>([this](KAsync::Future<void> &f) { waitForDrained(f, mSynchronizerQueue); })
        .then<void>([this](KAsync::Future<void> &f) { waitForDrained(f, mUserQueue); })
        .then<void>([this](KAsync::Future<void> &f) {
            if (mSynchronizer->allChangesReplayed()) {
                f.setFinished();
            } else {
                auto context = new QObject;
                QObject::connect(mSynchronizer.data(), &ChangeReplay::changesReplayed, context, [&f, context]() {
                    delete context;
                    f.setFinished();
                });
            }
        });
}

void GenericResource::updateLowerBoundRevision()
{
    mProcessor->setOldestUsedRevision(qMin(mClientLowerBoundRevision, mSynchronizer->getLastReplayedRevision()));
}

void GenericResource::setLowerBoundRevision(qint64 revision)
{
    mClientLowerBoundRevision = revision;
    updateLowerBoundRevision();
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "genericresource.moc"
#pragma clang diagnostic pop
