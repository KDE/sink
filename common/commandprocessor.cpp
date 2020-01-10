/*
 * Copyright (C) 2016 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include "commandprocessor.h"

#include <QDataStream>
#include <QCoreApplication>

#include "commands.h"
#include "messagequeue.h"
#include "flush_generated.h"
#include "inspector.h"
#include "synchronizer.h"
#include "pipeline.h"
#include "bufferutils.h"
#include "definitions.h"
#include "storage.h"

#include "queuedcommand_generated.h"
#include "revisionreplayed_generated.h"
#include "synchronize_generated.h"

static int sBatchSize = 100;
// This interval directly affects the roundtrip time of single commands
static int sCommitInterval = 10;


using namespace Sink;
using namespace Sink::Storage;

CommandProcessor::CommandProcessor(Sink::Pipeline *pipeline, const QByteArray &instanceId, const Sink::Log::Context &ctx)
    : QObject(),
    mLogCtx(ctx.subContext("commandprocessor")),
    mPipeline(pipeline),
    mUserQueue(Sink::storageLocation(), instanceId + ".userqueue"),
    mSynchronizerQueue(Sink::storageLocation(), instanceId + ".synchronizerqueue"),
    mCommandQueues({&mUserQueue, &mSynchronizerQueue}), mProcessingLock(false), mLowerBoundRevision(0)
{
    for (auto queue : mCommandQueues) {
        /*
         * This is a queued connection because otherwise we would execute CommandProcessor::process in the middle of
         * Synchronizer::commit, which is not what we want.
         */
        const bool ret = connect(queue, &MessageQueue::messageReady, this, &CommandProcessor::process, Qt::QueuedConnection);
        Q_UNUSED(ret);
    }

    mCommitQueueTimer.setInterval(sCommitInterval);
    mCommitQueueTimer.setSingleShot(true);
    QObject::connect(&mCommitQueueTimer, &QTimer::timeout, &mUserQueue, &MessageQueue::commit);
}

static void enqueueCommand(MessageQueue &mq, int commandId, const QByteArray &data)
{
    // qWarning() << "Enquing command " << mq.name();
    flatbuffers::FlatBufferBuilder fbb;
    auto commandData = Sink::EntityBuffer::appendAsVector(fbb, data.constData(), data.size());
    auto buffer = Sink::CreateQueuedCommand(fbb, commandId, commandData);
    Sink::FinishQueuedCommandBuffer(fbb, buffer);
    mq.enqueue(fbb.GetBufferPointer(), fbb.GetSize());
}

void CommandProcessor::processCommand(int commandId, const QByteArray &data)
{
    switch (commandId) {
        case Commands::FlushCommand:
            processFlushCommand(data);
            break;
        case Commands::SynchronizeCommand:
            processSynchronizeCommand(data);
            break;
        case Commands::AbortSynchronizationCommand:
            mSynchronizer->abort();
            break;
        // case Commands::RevisionReplayedCommand:
        //     processRevisionReplayedCommand(data);
        //     break;
        default: {
            static int modifications = 0;
            mUserQueue.startTransaction();
            SinkWarningCtx(mLogCtx) << "Received a command" << commandId;
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
    };
}

void CommandProcessor::processFlushCommand(const QByteArray &data)
{
    flatbuffers::Verifier verifier((const uint8_t *)data.constData(), data.size());
    if (Sink::Commands::VerifyFlushBuffer(verifier)) {
        auto buffer = Sink::Commands::GetFlush(data.constData());
        const auto flushType = buffer->type();
        const auto flushId = BufferUtils::extractBufferCopy(buffer->id());
        SinkTraceCtx(mLogCtx) << "Received flush command " << flushId;
        if (flushType == Sink::Flush::FlushSynchronization) {
            mSynchronizer->flush(flushType, flushId);
        } else {
            mUserQueue.startTransaction();
            enqueueCommand(mUserQueue, Commands::FlushCommand, data);
            mUserQueue.commit();
        }
    }

}

void CommandProcessor::processSynchronizeCommand(const QByteArray &data)
{
    flatbuffers::Verifier verifier((const uint8_t *)data.constData(), data.size());
    if (Sink::Commands::VerifySynchronizeBuffer(verifier)) {
        auto buffer = Sink::Commands::GetSynchronize(data.constData());
        Sink::QueryBase query;
        if (buffer->query()) {
            auto data = QByteArray::fromStdString(buffer->query()->str());
            QDataStream stream(&data, QIODevice::ReadOnly);
            stream >> query;
        }
        //Avoid blocking this call
        QMetaObject::invokeMethod(mSynchronizer.data(), [=] {
            mSynchronizer->synchronize(query);
        }, Qt::QueuedConnection);
    } else {
        SinkWarningCtx(mLogCtx) << "received invalid command";
    }
}

// void CommandProcessor::processRevisionReplayedCommand(const QByteArray &data)
// {
//     flatbuffers::Verifier verifier((const uint8_t *)commandBuffer.constData(), commandBuffer.size());
//     if (Sink::Commands::VerifyRevisionReplayedBuffer(verifier)) {
//         auto buffer = Sink::Commands::GetRevisionReplayed(commandBuffer.constData());
//         client.currentRevision = buffer->revision();
//     } else {
//         SinkWarningCtx(mLogCtx) << "received invalid command";
//     }
//     loadResource().setLowerBoundRevision(lowerBoundRevision());
// }

void CommandProcessor::setOldestUsedRevision(qint64 revision)
{
    mLowerBoundRevision = revision;
}

bool CommandProcessor::messagesToProcessAvailable()
{
    for (auto queue : mCommandQueues) {
        if (!queue->isEmpty()) {
            return true;
        }
    }
    return false;
}

void CommandProcessor::process()
{
    if (mProcessingLock) {
        return;
    }
    mProcessingLock = true;
    processPipeline()
        .then([this]() {
            mProcessingLock = false;
            //TODO process some events?
            if (messagesToProcessAvailable()) {
                process();
            }
        })
        .exec();
}

KAsync::Job<qint64> CommandProcessor::processQueuedCommand(const Sink::QueuedCommand &queuedCommand)
{
    SinkTraceCtx(mLogCtx) << "Processing command: " << Sink::Commands::name(queuedCommand.commandId());
    const auto data = queuedCommand.command()->Data();
    const auto size = queuedCommand.command()->size();
    switch (queuedCommand.commandId()) {
        case Sink::Commands::DeleteEntityCommand:
            return mPipeline->deletedEntity(data, size);
        case Sink::Commands::ModifyEntityCommand:
            return mPipeline->modifiedEntity(data, size);
        case Sink::Commands::CreateEntityCommand:
            return mPipeline->newEntity(data, size);
        case Sink::Commands::InspectionCommand:
            Q_ASSERT(mInspector);
            return mInspector->processCommand(data, size)
                    .then(KAsync::value<qint64>(-1));
        case Sink::Commands::FlushCommand:
            return flush(data, size)
                .then(KAsync::value<qint64>(-1));
        default:
            return KAsync::error<qint64>(-1, "Unhandled command");
    }
}

KAsync::Job<qint64> CommandProcessor::processQueuedCommand(const QByteArray &data)
{
    flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(data.constData()), data.size());
    if (!Sink::VerifyQueuedCommandBuffer(verifyer)) {
        SinkWarningCtx(mLogCtx) << "invalid buffer";
        return KAsync::error<qint64>(-1, "Invalid Buffer");
    }
    const auto queuedCommand = Sink::GetQueuedCommand(data.constData());
    const auto commandId = queuedCommand->commandId();
    return processQueuedCommand(*queuedCommand)
        .then<qint64, qint64>(
            [this, commandId](const KAsync::Error &error, qint64 createdRevision) -> KAsync::Job<qint64> {
                if (error) {
                    SinkWarningCtx(mLogCtx) << "Error while processing queue command: " << error.errorMessage;
                    return KAsync::error<qint64>(error);
                }
                SinkTraceCtx(mLogCtx) << "Command pipeline processed: " << Sink::Commands::name(commandId);
                return KAsync::value<qint64>(createdRevision);
            });
}

// Process one batch of messages from this queue
KAsync::Job<void> CommandProcessor::processQueue(MessageQueue *queue)
{
    auto time = QSharedPointer<QTime>::create();
    return KAsync::start([=] { mPipeline->startTransaction(); })
        .then([=] {
                return queue->dequeueBatch(sBatchSize,
                    [=](const QByteArray &data) {
                        time->start();
                        return processQueuedCommand(data)
                        .then([=](qint64 createdRevision) {
                            SinkTraceCtx(mLogCtx) << "Created revision " << createdRevision << ". Processing took: " << Log::TraceTime(time->elapsed());
                        });
                    })
                    .then([=](const KAsync::Error &error) {
                        if (error) {
                            if (error.errorCode != MessageQueue::ErrorCodes::NoMessageFound) {
                                SinkWarningCtx(mLogCtx) << "Error while getting message from messagequeue: " << error.errorMessage;
                            }
                        }
                    });
            })
        .then([=](const KAsync::Error &) {
            mPipeline->commit();
            //The flushed content has been persistet, we can notify the world
            for (const auto &flushId : mCompleteFlushes) {
                SinkTraceCtx(mLogCtx) << "Emitting flush completion" << flushId;
                mSynchronizer->flushComplete(flushId);
                Sink::Notification n;
                n.type = Sink::Notification::FlushCompletion;
                n.id = flushId;
                emit notify(n);
            }
            mCompleteFlushes.clear();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        });


}

KAsync::Job<void> CommandProcessor::processPipeline()
{
    auto time = QSharedPointer<QTime>::create();
    time->start();
    mPipeline->cleanupRevisions(mLowerBoundRevision);
    SinkTraceCtx(mLogCtx) << "Cleanup done." << Log::TraceTime(time->elapsed());

    // Go through all message queues
    if (mCommandQueues.isEmpty()) {
        return KAsync::null<void>();
    }
    return KAsync::doWhile([this]() {
            for (auto queue : mCommandQueues) {
                if (!queue->isEmpty()) {
                    // SinkLogCtx(mLogCtx) << "Processing queue" << queue->name();
                    auto time = QSharedPointer<QTime>::create();
                    time->start();
                    return processQueue(queue)
                        .then([=] {
                            SinkLogCtx(mLogCtx) << "Queue processed." << queue->name() << Log::TraceTime(time->elapsed());
                            return KAsync::Continue;
                        });
                } else {
                    // SinkLogCtx(mLogCtx) << "Nothing in queue" << queue->name();
                }
            }
            return KAsync::value(KAsync::Break);
        });
}

void CommandProcessor::setInspector(const QSharedPointer<Inspector> &inspector)
{
    mInspector = inspector;
    QObject::connect(mInspector.data(), &Inspector::notify, this, &CommandProcessor::notify);
}

void CommandProcessor::setSynchronizer(const QSharedPointer<Synchronizer> &synchronizer)
{
    mSynchronizer = synchronizer;
    mSynchronizer->setup([this](int commandId, const QByteArray &data) {
        enqueueCommand(mSynchronizerQueue, commandId, data);
    }, mSynchronizerQueue);
    QObject::connect(mSynchronizer.data(), &Synchronizer::notify, this, &CommandProcessor::notify);
    setOldestUsedRevision(mSynchronizer->getLastReplayedRevision());
}

KAsync::Job<void> CommandProcessor::flush(void const *command, size_t size)
{
    flatbuffers::Verifier verifier((const uint8_t *)command, size);
    if (Sink::Commands::VerifyFlushBuffer(verifier)) {
        auto buffer = Sink::Commands::GetFlush(command);
        const auto flushType = buffer->type();
        const QByteArray flushId = BufferUtils::extractBufferCopy(buffer->id());
        Q_ASSERT(!flushId.isEmpty());
        if (flushType == Sink::Flush::FlushReplayQueue) {
            SinkTraceCtx(mLogCtx) << "Flushing synchronizer " << flushId;
            Q_ASSERT(mSynchronizer);
            mSynchronizer->flush(flushType, flushId);
        } else {
            //Defer notification until the results have been comitted
            mCompleteFlushes << flushId;
        }
        return KAsync::null<void>();
    }
    return KAsync::error<void>(-1, "Invalid flush command.");
}

static void waitForDrained(KAsync::Future<void> &f, MessageQueue &queue)
{
    if (queue.isEmpty()) {
        f.setFinished();
    } else {
        auto context = new QObject;
        QObject::connect(&queue, &MessageQueue::drained, context, [&f, context]() {
            delete context;
            f.setFinished();
        });
    }
};

KAsync::Job<void> CommandProcessor::processAllMessages()
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
            if (mSynchronizer->ChangeReplay::allChangesReplayed()) {
                f.setFinished();
            } else {
                auto context = new QObject;
                QObject::connect(mSynchronizer.data(), &ChangeReplay::changesReplayed, context, [&f, context]() {
                    delete context;
                    f.setFinished();
                });
                mSynchronizer->replayNextRevision().exec();
            }
        });
}

