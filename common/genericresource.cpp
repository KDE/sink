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
#include "domainadaptor.h"
#include "commands.h"
#include "index.h"
#include "log.h"
#include "definitions.h"
#include "bufferutils.h"
#include "adaptorfactoryregistry.h"
#include "synchronizer.h"

#include <QUuid>
#include <QDataStream>
#include <QTime>

static int sBatchSize = 100;
// This interval directly affects the roundtrip time of single commands
static int sCommitInterval = 10;

using namespace Sink;
using namespace Sink::Storage;

/**
 * Drives the pipeline using the output from all command queues
 */
class CommandProcessor : public QObject
{
    Q_OBJECT
    typedef std::function<KAsync::Job<void>(void const *, size_t)> InspectionFunction;
    SINK_DEBUG_AREA("commandprocessor")

public:
    CommandProcessor(Sink::Pipeline *pipeline, QList<MessageQueue *> commandQueues) : QObject(), mPipeline(pipeline), mCommandQueues(commandQueues), mProcessingLock(false), mLowerBoundRevision(0)
    {
        for (auto queue : mCommandQueues) {
            const bool ret = connect(queue, &MessageQueue::messageReady, this, &CommandProcessor::process);
            Q_UNUSED(ret);
        }
    }

    void setOldestUsedRevision(qint64 revision)
    {
        mLowerBoundRevision = revision;
    }

    void setInspectionCommand(const InspectionFunction &f)
    {
        mInspect = f;
    }

signals:
    void error(int errorCode, const QString &errorMessage);

private:
    bool messagesToProcessAvailable()
    {
        for (auto queue : mCommandQueues) {
            if (!queue->isEmpty()) {
                return true;
            }
        }
        return false;
    }

private slots:
    void process()
    {
        if (mProcessingLock) {
            return;
        }
        mProcessingLock = true;
        auto job = processPipeline()
                       .syncThen<void>([this]() {
                           mProcessingLock = false;
                           if (messagesToProcessAvailable()) {
                               process();
                           }
                       })
                       .exec();
    }

    KAsync::Job<qint64> processQueuedCommand(const Sink::QueuedCommand *queuedCommand)
    {
        SinkTrace() << "Processing command: " << Sink::Commands::name(queuedCommand->commandId());
        // Throw command into appropriate pipeline
        switch (queuedCommand->commandId()) {
            case Sink::Commands::DeleteEntityCommand:
                return mPipeline->deletedEntity(queuedCommand->command()->Data(), queuedCommand->command()->size());
            case Sink::Commands::ModifyEntityCommand:
                return mPipeline->modifiedEntity(queuedCommand->command()->Data(), queuedCommand->command()->size());
            case Sink::Commands::CreateEntityCommand:
                return mPipeline->newEntity(queuedCommand->command()->Data(), queuedCommand->command()->size());
            case Sink::Commands::InspectionCommand:
                if (mInspect) {
                    return mInspect(queuedCommand->command()->Data(), queuedCommand->command()->size())
                        .syncThen<qint64>([]() { return -1; });
                } else {
                    return KAsync::error<qint64>(-1, "Missing inspection command.");
                }
            default:
                return KAsync::error<qint64>(-1, "Unhandled command");
        }
    }

    KAsync::Job<qint64> processQueuedCommand(const QByteArray &data)
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(data.constData()), data.size());
        if (!Sink::VerifyQueuedCommandBuffer(verifyer)) {
            SinkWarning() << "invalid buffer";
            // return KAsync::error<void, qint64>(1, "Invalid Buffer");
        }
        auto queuedCommand = Sink::GetQueuedCommand(data.constData());
        const auto commandId = queuedCommand->commandId();
        SinkTrace() << "Dequeued Command: " << Sink::Commands::name(commandId);
        return processQueuedCommand(queuedCommand)
            .then<qint64, qint64>(
                [this, commandId](const KAsync::Error &error, qint64 createdRevision) -> KAsync::Job<qint64> {
                    if (error) {
                        SinkWarning() << "Error while processing queue command: " << error.errorMessage;
                        return KAsync::error<qint64>(error);
                    }
                    SinkTrace() << "Command pipeline processed: " << Sink::Commands::name(commandId);
                    return KAsync::value<qint64>(createdRevision);
                });
    }

    // Process all messages of this queue
    KAsync::Job<void> processQueue(MessageQueue *queue)
    {
        auto time = QSharedPointer<QTime>::create();
        return KAsync::syncStart<void>([this]() { mPipeline->startTransaction(); })
            .then(KAsync::dowhile(
                [this, queue, time]() -> KAsync::Job<KAsync::ControlFlowFlag> {
                    return queue->dequeueBatch(sBatchSize,
                        [this, time](const QByteArray &data) -> KAsync::Job<void> {
                            time->start();
                            return processQueuedCommand(data)
                            .syncThen<void, qint64>([this, time](qint64 createdRevision) {
                                SinkTrace() << "Created revision " << createdRevision << ". Processing took: " << Log::TraceTime(time->elapsed());
                            });
                        })
                        .then<KAsync::ControlFlowFlag>([queue](const KAsync::Error &error) {
                                if (error) {
                                    if (error.errorCode != MessageQueue::ErrorCodes::NoMessageFound) {
                                        SinkWarning() << "Error while getting message from messagequeue: " << error.errorMessage;
                                    }
                                }
                                if (queue->isEmpty()) {
                                    return KAsync::value<KAsync::ControlFlowFlag>(KAsync::Break);
                                } else {
                                    return KAsync::value<KAsync::ControlFlowFlag>(KAsync::Continue);
                                }
                            });
                }))
            .syncThen<void>([this](const KAsync::Error &) { mPipeline->commit(); });
    }

    KAsync::Job<void> processPipeline()
    {
        auto time = QSharedPointer<QTime>::create();
        time->start();
        mPipeline->startTransaction();
        mPipeline->cleanupRevisions(mLowerBoundRevision);
        mPipeline->commit();
        SinkTrace() << "Cleanup done." << Log::TraceTime(time->elapsed());

        // Go through all message queues
        auto it = QSharedPointer<QListIterator<MessageQueue *>>::create(mCommandQueues);
        return KAsync::dowhile(
            [it, this]() {
                auto time = QSharedPointer<QTime>::create();
                time->start();

                auto queue = it->next();
                return processQueue(queue)
                    .syncThen<KAsync::ControlFlowFlag>([this, time, it]() {
                        SinkTrace() << "Queue processed." << Log::TraceTime(time->elapsed());
                        if (it->hasNext()) {
                            return KAsync::Continue;
                        }
                        return KAsync::Break;
                    });
            });
    }

private:
    Sink::Pipeline *mPipeline;
    // Ordered by priority
    QList<MessageQueue *> mCommandQueues;
    bool mProcessingLock;
    // The lowest revision we no longer need
    qint64 mLowerBoundRevision;
    InspectionFunction mInspect;
};

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
    mProcessor->setInspectionCommand([this](void const *command, size_t size) {
        flatbuffers::Verifier verifier((const uint8_t *)command, size);
        if (Sink::Commands::VerifyInspectionBuffer(verifier)) {
            auto buffer = Sink::Commands::GetInspection(command);
            int inspectionType = buffer->type();

            QByteArray inspectionId = BufferUtils::extractBuffer(buffer->id());
            QByteArray entityId = BufferUtils::extractBuffer(buffer->entityId());
            QByteArray domainType = BufferUtils::extractBuffer(buffer->domainType());
            QByteArray property = BufferUtils::extractBuffer(buffer->property());
            QByteArray expectedValueString = BufferUtils::extractBuffer(buffer->expectedValue());
            QDataStream s(expectedValueString);
            QVariant expectedValue;
            s >> expectedValue;
            inspect(inspectionType, inspectionId, domainType, entityId, property, expectedValue)
                .then<void>(
                    [=](const KAsync::Error &error) {
                        Sink::Notification n;
                        n.type = Sink::Notification::Inspection;
                        n.id = inspectionId;
                        if (error) {
                            Warning_area("resource.inspection") << "Inspection failed: " << inspectionType << inspectionId << entityId << error.errorMessage;
                            n.code = Sink::Notification::Failure;
                        } else {
                            Log_area("resource.inspection") << "Inspection was successful: " << inspectionType << inspectionId << entityId;
                            n.code = Sink::Notification::Success;
                        }
                        emit notify(n);
                        return KAsync::null();
                    })
                .exec();
            return KAsync::null<void>();
        }
        return KAsync::error<void>(-1, "Invalid inspection command.");
    });
    {
        auto ret =QObject::connect(mProcessor.get(), &CommandProcessor::error, [this](int errorCode, const QString &msg) { onProcessorError(errorCode, msg); });
        Q_ASSERT(ret);
    }
    {
        auto ret = QObject::connect(mPipeline.data(), &Pipeline::revisionUpdated, this, &Resource::revisionUpdated);
        Q_ASSERT(ret);
    }

    mCommitQueueTimer.setInterval(sCommitInterval);
    mCommitQueueTimer.setSingleShot(true);
    QObject::connect(&mCommitQueueTimer, &QTimer::timeout, &mUserQueue, &MessageQueue::commit);
}

GenericResource::~GenericResource()
{
}

KAsync::Job<void> GenericResource::inspect(
    int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
{
    SinkWarning() << "Inspection not implemented";
    return KAsync::null<void>();
}

void GenericResource::enableChangeReplay(bool enable)
{
    Q_ASSERT(mChangeReplay);
    if (enable) {
        QObject::connect(mPipeline.data(), &Pipeline::revisionUpdated, mChangeReplay.data(), &ChangeReplay::revisionChanged, Qt::QueuedConnection);
        QObject::connect(mChangeReplay.data(), &ChangeReplay::changesReplayed, this, &GenericResource::updateLowerBoundRevision);
        QMetaObject::invokeMethod(mChangeReplay.data(), "revisionChanged", Qt::QueuedConnection);
    } else {
        QObject::disconnect(mPipeline.data(), &Pipeline::revisionUpdated, mChangeReplay.data(), &ChangeReplay::revisionChanged);
        QObject::disconnect(mChangeReplay.data(), &ChangeReplay::changesReplayed, this, &GenericResource::updateLowerBoundRevision);
    }
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
}

void GenericResource::setupChangereplay(const QSharedPointer<ChangeReplay> &changeReplay)
{
    mChangeReplay = changeReplay;
    {
        auto ret = QObject::connect(mChangeReplay.data(), &ChangeReplay::replayingChanges, [this]() {
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
        auto ret = QObject::connect(mChangeReplay.data(), &ChangeReplay::changesReplayed, [this]() {
            Sink::Notification n;
            n.id = "changereplay";
            n.type = Sink::Notification::Status;
            n.message = "All changes have been replayed.";
            n.code = Sink::ApplicationDomain::ConnectedStatus;
            emit notify(n);
        });
        Q_ASSERT(ret);
    }

    mProcessor->setOldestUsedRevision(mChangeReplay->getLastReplayedRevision());
    enableChangeReplay(true);
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
        // Changereplay would deadlock otherwise when trying to open the synchronization store
        enableChangeReplay(false);
        return mSynchronizer->synchronize(query)
            .then<void>([this](const KAsync::Error &error) {
                enableChangeReplay(true);
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
            if (mChangeReplay->allChangesReplayed()) {
                f.setFinished();
            } else {
                auto context = new QObject;
                QObject::connect(mChangeReplay.data(), &ChangeReplay::changesReplayed, context, [&f, context]() {
                    delete context;
                    f.setFinished();
                });
            }
        });
}

void GenericResource::updateLowerBoundRevision()
{
    mProcessor->setOldestUsedRevision(qMin(mClientLowerBoundRevision, mChangeReplay->getLastReplayedRevision()));
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
