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

/**
 * Drives the pipeline using the output from all command queues
 */
class CommandProcessor : public QObject
{
    Q_OBJECT
    typedef std::function<KAsync::Job<void>(void const *, size_t)> InspectionFunction;
    SINK_DEBUG_AREA("commandprocessor")

public:
    CommandProcessor(Sink::Pipeline *pipeline, QList<MessageQueue *> commandQueues) : QObject(), mPipeline(pipeline), mCommandQueues(commandQueues), mProcessingLock(false)
    {
        mLowerBoundRevision = Storage::maxRevision(mPipeline->storage().createTransaction(Storage::ReadOnly, [](const Sink::Storage::Error &error) {
            SinkWarning() << error.message;
        }));

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
                       .then<void>([this]() {
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
                    return mInspect(queuedCommand->command()->Data(), queuedCommand->command()->size()).then<qint64>([]() { return -1; });
                } else {
                    return KAsync::error<qint64>(-1, "Missing inspection command.");
                }
            default:
                return KAsync::error<qint64>(-1, "Unhandled command");
        }
    }

    KAsync::Job<qint64, qint64> processQueuedCommand(const QByteArray &data)
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
                [this, commandId](qint64 createdRevision) -> qint64 {
                    SinkTrace() << "Command pipeline processed: " << Sink::Commands::name(commandId);
                    return createdRevision;
                },
                [](int errorCode, QString errorMessage) {
                    // FIXME propagate error, we didn't handle it
                    SinkWarning() << "Error while processing queue command: " << errorMessage;
                });
    }

    // Process all messages of this queue
    KAsync::Job<void> processQueue(MessageQueue *queue)
    {
        auto time = QSharedPointer<QTime>::create();
        return KAsync::start<void>([this]() { mPipeline->startTransaction(); })
            .then(KAsync::dowhile([queue]() { return !queue->isEmpty(); },
                [this, queue, time](KAsync::Future<void> &future) {
                    queue->dequeueBatch(sBatchSize,
                             [this, time](const QByteArray &data) {
                                 time->start();
                                 return KAsync::start<void>([this, data, time](KAsync::Future<void> &future) {
                                     processQueuedCommand(data)
                                         .then<void, qint64>([&future, this, time](qint64 createdRevision) {
                                             SinkTrace() << "Created revision " << createdRevision << ". Processing took: " << Log::TraceTime(time->elapsed());
                                             future.setFinished();
                                         })
                                         .exec();
                                 });
                             })
                        .then<void>([&future, queue]() { future.setFinished(); },
                            [&future](int i, QString error) {
                                if (i != MessageQueue::ErrorCodes::NoMessageFound) {
                                    SinkWarning() << "Error while getting message from messagequeue: " << error;
                                }
                                future.setFinished();
                            })
                        .exec();
                }))
            .then<void>([this]() { mPipeline->commit(); });
    }

    KAsync::Job<void> processPipeline()
    {
        auto time = QSharedPointer<QTime>::create();
        time->start();
        mPipeline->startTransaction();
        SinkTrace() << "Cleaning up from " << mPipeline->cleanedUpRevision() + 1 << " to " << mLowerBoundRevision;
        for (qint64 revision = mPipeline->cleanedUpRevision() + 1; revision <= mLowerBoundRevision; revision++) {
            mPipeline->cleanupRevision(revision);
        }
        mPipeline->commit();
        SinkTrace() << "Cleanup done." << Log::TraceTime(time->elapsed());

        // Go through all message queues
        auto it = QSharedPointer<QListIterator<MessageQueue *>>::create(mCommandQueues);
        return KAsync::dowhile([it]() { return it->hasNext(); },
            [it, this](KAsync::Future<void> &future) {
                auto time = QSharedPointer<QTime>::create();
                time->start();

                auto queue = it->next();
                processQueue(queue)
                    .then<void>([this, &future, time]() {
                        SinkTrace() << "Queue processed." << Log::TraceTime(time->elapsed());
                        future.setFinished();
                    })
                    .exec();
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

GenericResource::GenericResource(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier, const QSharedPointer<Pipeline> &pipeline )
    : Sink::Resource(),
      mUserQueue(Sink::storageLocation(), resourceInstanceIdentifier + ".userqueue"),
      mSynchronizerQueue(Sink::storageLocation(), resourceInstanceIdentifier + ".synchronizerqueue"),
      mResourceType(resourceType),
      mResourceInstanceIdentifier(resourceInstanceIdentifier),
      mPipeline(pipeline ? pipeline : QSharedPointer<Sink::Pipeline>::create(resourceInstanceIdentifier)),
      mError(0),
      mClientLowerBoundRevision(std::numeric_limits<qint64>::max())
{
    mPipeline->setResourceType(mResourceType);
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
                    [=]() {
                        Log_area("resource.inspection") << "Inspection was successful: " << inspectionType << inspectionId << entityId;
                        Sink::Notification n;
                        n.type = Sink::Notification::Inspection;
                        n.id = inspectionId;
                        n.code = Sink::Notification::Success;
                        emit notify(n);
                    },
                    [=](int code, const QString &message) {
                        Warning_area("resource.inspection") << "Inspection failed: " << inspectionType << inspectionId << entityId << message;
                        Sink::Notification n;
                        n.type = Sink::Notification::Inspection;
                        n.message = message;
                        n.id = inspectionId;
                        n.code = Sink::Notification::Failure;
                        emit notify(n);
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
    mClientLowerBoundRevision = mPipeline->cleanedUpRevision();

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

void GenericResource::removeDataFromDisk()
{
    SinkLog() << "Removing the resource from disk: " << mResourceInstanceIdentifier;
    //Ensure we have no transaction or databases open
    mSynchronizer.clear();
    mChangeReplay.clear();
    mPipeline.clear();
    removeFromDisk(mResourceInstanceIdentifier);
}

void GenericResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    Sink::Storage(Sink::storageLocation(), instanceIdentifier, Sink::Storage::ReadWrite).removeFromDisk();
    Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".userqueue", Sink::Storage::ReadWrite).removeFromDisk();
    Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".synchronizerqueue", Sink::Storage::ReadWrite).removeFromDisk();
    Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".changereplay", Sink::Storage::ReadWrite).removeFromDisk();
    Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".synchronization", Sink::Storage::ReadWrite).removeFromDisk();
}

qint64 GenericResource::diskUsage(const QByteArray &instanceIdentifier)
{
    auto size = Sink::Storage(Sink::storageLocation(), instanceIdentifier, Sink::Storage::ReadOnly).diskUsage();
    size += Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".userqueue", Sink::Storage::ReadOnly).diskUsage();
    size += Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".synchronizerqueue", Sink::Storage::ReadOnly).diskUsage();
    size += Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".changereplay", Sink::Storage::ReadOnly).diskUsage();
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

KAsync::Job<void> GenericResource::synchronizeWithSource()
{
    return KAsync::start<void>([this](KAsync::Future<void> &future) {

        Sink::Notification n;
        n.id = "sync";
        n.type = Sink::Notification::Status;
        n.message = "Synchronization has started.";
        n.code = Sink::ApplicationDomain::BusyStatus;
        emit notify(n);

        SinkLog() << " Synchronizing";
        // Changereplay would deadlock otherwise when trying to open the synchronization store
        enableChangeReplay(false);
        mSynchronizer->synchronize()
            .then<void>([this, &future]() {
                SinkLog() << "Done Synchronizing";
                Sink::Notification n;
                n.id = "sync";
                n.type = Sink::Notification::Status;
                n.message = "Synchronization has ended.";
                n.code = Sink::ApplicationDomain::ConnectedStatus;
                emit notify(n);

                enableChangeReplay(true);
                future.setFinished();
            }, [this, &future](int errorCode, const QString &error) {
                enableChangeReplay(true);
                future.setError(errorCode, error);
            })
            .exec();
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
