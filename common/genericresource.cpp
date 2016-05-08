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

#include <QUuid>
#include <QDataStream>
#include <QTime>

static int sBatchSize = 100;
// This interval directly affects the roundtrip time of single commands
static int sCommitInterval = 10;

using namespace Sink;

#undef DEBUG_AREA
#define DEBUG_AREA "resource.changereplay"

/**
 * Replays changes from the storage one by one.
 *
 * Uses a local database to:
 * * Remember what changes have been replayed already.
 * * store a mapping of remote to local buffers
 */
class ChangeReplay : public QObject
{
    Q_OBJECT
public:
    typedef std::function<KAsync::Job<void>(const QByteArray &type, const QByteArray &key, const QByteArray &value)> ReplayFunction;

    ChangeReplay(const QString &resourceName, const ReplayFunction &replayFunction)
        : mStorage(storageLocation(), resourceName, Storage::ReadOnly), mChangeReplayStore(storageLocation(), resourceName + ".changereplay", Storage::ReadWrite), mReplayFunction(replayFunction)
    {
    }

    qint64 getLastReplayedRevision()
    {
        qint64 lastReplayedRevision = 0;
        auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadOnly);
        replayStoreTransaction.openDatabase().scan("lastReplayedRevision",
            [&lastReplayedRevision](const QByteArray &key, const QByteArray &value) -> bool {
                lastReplayedRevision = value.toLongLong();
                return false;
            },
            [](const Storage::Error &) {});
        return lastReplayedRevision;
    }

    bool allChangesReplayed()
    {
        const qint64 topRevision = Storage::maxRevision(mStorage.createTransaction(Storage::ReadOnly));
        const qint64 lastReplayedRevision = getLastReplayedRevision();
        Trace() << "All changes replayed " << topRevision << lastReplayedRevision;
        return (lastReplayedRevision >= topRevision);
    }

signals:
    void changesReplayed();

public slots:
    void revisionChanged()
    {
        auto mainStoreTransaction = mStorage.createTransaction(Storage::ReadOnly);
        auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadWrite);
        qint64 lastReplayedRevision = 1;
        replayStoreTransaction.openDatabase().scan("lastReplayedRevision",
            [&lastReplayedRevision](const QByteArray &key, const QByteArray &value) -> bool {
                lastReplayedRevision = value.toLongLong();
                return false;
            },
            [](const Storage::Error &) {});
        const qint64 topRevision = Storage::maxRevision(mainStoreTransaction);

        Trace() << "Changereplay from " << lastReplayedRevision << " to " << topRevision;
        if (lastReplayedRevision <= topRevision) {
            qint64 revision = lastReplayedRevision;
            for (; revision <= topRevision; revision++) {
                const auto uid = Storage::getUidFromRevision(mainStoreTransaction, revision);
                const auto type = Storage::getTypeFromRevision(mainStoreTransaction, revision);
                const auto key = Storage::assembleKey(uid, revision);
                Storage::mainDatabase(mainStoreTransaction, type)
                    .scan(key,
                        [&lastReplayedRevision, type, this](const QByteArray &key, const QByteArray &value) -> bool {
                            mReplayFunction(type, key, value).exec();
                            // TODO make for loop async, and pass to async replay function together with type
                            Trace() << "Replaying " << key;
                            return false;
                        },
                        [key](const Storage::Error &) { ErrorMsg() << "Failed to replay change " << key; });
            }
            revision--;
            replayStoreTransaction.openDatabase().write("lastReplayedRevision", QByteArray::number(revision));
            replayStoreTransaction.commit();
            Trace() << "Replayed until " << revision;
        }
        emit changesReplayed();
    }

private:
    Sink::Storage mStorage;
    Sink::Storage mChangeReplayStore;
    ReplayFunction mReplayFunction;
};

#undef DEBUG_AREA
#define DEBUG_AREA "resource.commandprocessor"

/**
 * Drives the pipeline using the output from all command queues
 */
class CommandProcessor : public QObject
{
    Q_OBJECT
    typedef std::function<KAsync::Job<void>(void const *, size_t)> InspectionFunction;

public:
    CommandProcessor(Sink::Pipeline *pipeline, QList<MessageQueue *> commandQueues) : QObject(), mPipeline(pipeline), mCommandQueues(commandQueues), mProcessingLock(false)
    {
        mPipeline->startTransaction();
        // FIXME Should be initialized to the current value of the change replay queue
        mLowerBoundRevision = Storage::maxRevision(mPipeline->transaction());
        mPipeline->commit();

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
        Log() << "Processing command: " << Sink::Commands::name(queuedCommand->commandId());
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
            Warning() << "invalid buffer";
            // return KAsync::error<void, qint64>(1, "Invalid Buffer");
        }
        auto queuedCommand = Sink::GetQueuedCommand(data.constData());
        const auto commandId = queuedCommand->commandId();
        Trace() << "Dequeued Command: " << Sink::Commands::name(commandId);
        return processQueuedCommand(queuedCommand)
            .then<qint64, qint64>(
                [commandId](qint64 createdRevision) -> qint64 {
                    Trace() << "Command pipeline processed: " << Sink::Commands::name(commandId);
                    return createdRevision;
                },
                [](int errorCode, QString errorMessage) {
                    // FIXME propagate error, we didn't handle it
                    Warning() << "Error while processing queue command: " << errorMessage;
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
                                             Trace() << "Created revision " << createdRevision << ". Processing took: " << Log::TraceTime(time->elapsed());
                                             future.setFinished();
                                         })
                                         .exec();
                                 });
                             })
                        .then<void>([&future, queue]() { future.setFinished(); },
                            [&future](int i, QString error) {
                                if (i != MessageQueue::ErrorCodes::NoMessageFound) {
                                    Warning() << "Error while getting message from messagequeue: " << error;
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
        Trace() << "Cleaning up from " << mPipeline->cleanedUpRevision() + 1 << " to " << mLowerBoundRevision;
        for (qint64 revision = mPipeline->cleanedUpRevision() + 1; revision <= mLowerBoundRevision; revision++) {
            mPipeline->cleanupRevision(revision);
        }
        mPipeline->commit();
        Trace() << "Cleanup done." << Log::TraceTime(time->elapsed());

        // Go through all message queues
        auto it = QSharedPointer<QListIterator<MessageQueue *>>::create(mCommandQueues);
        return KAsync::dowhile([it]() { return it->hasNext(); },
            [it, this](KAsync::Future<void> &future) {
                auto time = QSharedPointer<QTime>::create();
                time->start();

                auto queue = it->next();
                processQueue(queue)
                    .then<void>([&future, time]() {
                        Trace() << "Queue processed." << Log::TraceTime(time->elapsed());
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

#undef DEBUG_AREA
#define DEBUG_AREA "resource"

GenericResource::GenericResource(const QByteArray &resourceInstanceIdentifier, const QSharedPointer<Pipeline> &pipeline)
    : Sink::Resource(),
      mUserQueue(Sink::storageLocation(), resourceInstanceIdentifier + ".userqueue"),
      mSynchronizerQueue(Sink::storageLocation(), resourceInstanceIdentifier + ".synchronizerqueue"),
      mResourceInstanceIdentifier(resourceInstanceIdentifier),
      mPipeline(pipeline ? pipeline : QSharedPointer<Sink::Pipeline>::create(resourceInstanceIdentifier)),
      mError(0),
      mClientLowerBoundRevision(std::numeric_limits<qint64>::max())
{
    mProcessor = new CommandProcessor(mPipeline.data(), QList<MessageQueue *>() << &mUserQueue << &mSynchronizerQueue);
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
                        n.type = Sink::Commands::NotificationType_Inspection;
                        n.id = inspectionId;
                        n.code = Sink::Commands::NotificationCode_Success;
                        emit notify(n);
                    },
                    [=](int code, const QString &message) {
                        Log() << "Inspection failed: " << inspectionType << inspectionId << entityId << message;
                        Sink::Notification n;
                        n.type = Sink::Commands::NotificationType_Inspection;
                        n.message = message;
                        n.id = inspectionId;
                        n.code = Sink::Commands::NotificationCode_Failure;
                        emit notify(n);
                    })
                .exec();
            return KAsync::null<void>();
        }
        return KAsync::error<void>(-1, "Invalid inspection command.");
    });
    QObject::connect(mProcessor, &CommandProcessor::error, [this](int errorCode, const QString &msg) { onProcessorError(errorCode, msg); });
    QObject::connect(mPipeline.data(), &Pipeline::revisionUpdated, this, &Resource::revisionUpdated);
    mSourceChangeReplay = new ChangeReplay(resourceInstanceIdentifier, [this](const QByteArray &type, const QByteArray &key, const QByteArray &value) {
        // This results in a deadlock when a sync is in progress and we try to create a second writing transaction (which is why we turn changereplay off during the sync)
        auto synchronizationStore = QSharedPointer<Sink::Storage>::create(Sink::storageLocation(), mResourceInstanceIdentifier + ".synchronization", Sink::Storage::ReadWrite);
        return this->replay(*synchronizationStore, type, key, value).then<void>([synchronizationStore]() {});
    });
    enableChangeReplay(true);
    mClientLowerBoundRevision = mPipeline->cleanedUpRevision();
    mProcessor->setOldestUsedRevision(mSourceChangeReplay->getLastReplayedRevision());

    mCommitQueueTimer.setInterval(sCommitInterval);
    mCommitQueueTimer.setSingleShot(true);
    QObject::connect(&mCommitQueueTimer, &QTimer::timeout, &mUserQueue, &MessageQueue::commit);
}

GenericResource::~GenericResource()
{
    delete mProcessor;
    delete mSourceChangeReplay;
}

KAsync::Job<void> GenericResource::inspect(
    int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
{
    Warning() << "Inspection not implemented";
    return KAsync::null<void>();
}

void GenericResource::enableChangeReplay(bool enable)
{
    if (enable) {
        QObject::connect(mPipeline.data(), &Pipeline::revisionUpdated, mSourceChangeReplay, &ChangeReplay::revisionChanged, Qt::QueuedConnection);
        QObject::connect(mSourceChangeReplay, &ChangeReplay::changesReplayed, this, &GenericResource::updateLowerBoundRevision);
        mSourceChangeReplay->revisionChanged();
    } else {
        QObject::disconnect(mPipeline.data(), &Pipeline::revisionUpdated, mSourceChangeReplay, &ChangeReplay::revisionChanged);
        QObject::disconnect(mSourceChangeReplay, &ChangeReplay::changesReplayed, this, &GenericResource::updateLowerBoundRevision);
    }
}

void GenericResource::addType(const QByteArray &type, DomainTypeAdaptorFactoryInterface::Ptr factory, const QVector<Sink::Preprocessor *> &preprocessors)
{
    mPipeline->setPreprocessors(type, preprocessors);
    mPipeline->setAdaptorFactory(type, factory);
}

KAsync::Job<void> GenericResource::replay(Sink::Storage &synchronizationStore, const QByteArray &type, const QByteArray &key, const QByteArray &value)
{
    return KAsync::null<void>();
}

void GenericResource::removeDataFromDisk()
{
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
    Warning() << "Received error from Processor: " << errorCode << errorMessage;
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
    return KAsync::start<void>([this]() {
        Log() << " Synchronizing";
        // Changereplay would deadlock otherwise when trying to open the synchronization store
        enableChangeReplay(false);
        auto mainStore = QSharedPointer<Sink::Storage>::create(Sink::storageLocation(), mResourceInstanceIdentifier, Sink::Storage::ReadOnly);
        auto syncStore = QSharedPointer<Sink::Storage>::create(Sink::storageLocation(), mResourceInstanceIdentifier + ".synchronization", Sink::Storage::ReadWrite);
        synchronizeWithSource(*mainStore, *syncStore)
            .then<void>([this, mainStore, syncStore]() {
                Log() << "Done Synchronizing";
                enableChangeReplay(true);
            })
            .exec();
    });
}

KAsync::Job<void> GenericResource::synchronizeWithSource(Sink::Storage &mainStore, Sink::Storage &synchronizationStore)
{
    return KAsync::null<void>();
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
            if (mSourceChangeReplay->allChangesReplayed()) {
                f.setFinished();
            } else {
                auto context = new QObject;
                QObject::connect(mSourceChangeReplay, &ChangeReplay::changesReplayed, context, [&f, context]() {
                    delete context;
                    f.setFinished();
                });
            }
        });
}

void GenericResource::updateLowerBoundRevision()
{
    mProcessor->setOldestUsedRevision(qMin(mClientLowerBoundRevision, mSourceChangeReplay->getLastReplayedRevision()));
}

void GenericResource::setLowerBoundRevision(qint64 revision)
{
    mClientLowerBoundRevision = revision;
    updateLowerBoundRevision();
}

void GenericResource::createEntity(const QByteArray &sinkId, const QByteArray &bufferType, const Sink::ApplicationDomain::ApplicationDomainType &domainObject,
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

void GenericResource::modifyEntity(const QByteArray &sinkId, qint64 revision, const QByteArray &bufferType, const Sink::ApplicationDomain::ApplicationDomainType &domainObject,
    DomainTypeAdaptorFactoryInterface &adaptorFactory, std::function<void(const QByteArray &)> callback)
{
    // These changes are coming from the source
    const auto replayToSource = false;
    flatbuffers::FlatBufferBuilder entityFbb;
    adaptorFactory.createBuffer(domainObject, entityFbb);
    flatbuffers::FlatBufferBuilder fbb;
    auto entityId = fbb.CreateString(sinkId.toStdString());
    // This is the resource type and not the domain type
    auto type = fbb.CreateString(bufferType.toStdString());
    auto delta = Sink::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
    // TODO removals
    auto location = Sink::Commands::CreateModifyEntity(fbb, revision, entityId, 0, type, delta, replayToSource);
    Sink::Commands::FinishModifyEntityBuffer(fbb, location);
    callback(BufferUtils::extractBuffer(fbb));
}

void GenericResource::deleteEntity(const QByteArray &sinkId, qint64 revision, const QByteArray &bufferType, std::function<void(const QByteArray &)> callback)
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

void GenericResource::recordRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId, Sink::Storage::Transaction &transaction)
{
    Index("rid.mapping." + bufferType, transaction).add(remoteId, localId);
    ;
    Index("localid.mapping." + bufferType, transaction).add(localId, remoteId);
}

void GenericResource::removeRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId, Sink::Storage::Transaction &transaction)
{
    Index("rid.mapping." + bufferType, transaction).remove(remoteId, localId);
    Index("localid.mapping." + bufferType, transaction).remove(localId, remoteId);
}

void GenericResource::updateRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId, Sink::Storage::Transaction &transaction)
{
    const auto oldRemoteId = Index("localid.mapping." + bufferType, transaction).lookup(localId);
    removeRemoteId(bufferType, localId, oldRemoteId, transaction);
    recordRemoteId(bufferType, localId, remoteId, transaction);
}

QByteArray GenericResource::resolveRemoteId(const QByteArray &bufferType, const QByteArray &remoteId, Sink::Storage::Transaction &transaction)
{
    // Lookup local id for remote id, or insert a new pair otherwise
    Index index("rid.mapping." + bufferType, transaction);
    QByteArray sinkId = index.lookup(remoteId);
    if (sinkId.isEmpty()) {
        sinkId = QUuid::createUuid().toString().toUtf8();
        index.add(remoteId, sinkId);
        Index("localid.mapping." + bufferType, transaction).add(sinkId, remoteId);
    }
    return sinkId;
}

QByteArray GenericResource::resolveLocalId(const QByteArray &bufferType, const QByteArray &localId, Sink::Storage::Transaction &transaction)
{
    QByteArray remoteId = Index("localid.mapping." + bufferType, transaction).lookup(localId);
    if (remoteId.isEmpty()) {
        Warning() << "Couldn't find the remote id for " << localId;
        return QByteArray();
    }
    return remoteId;
}

void GenericResource::scanForRemovals(Sink::Storage::Transaction &transaction, Sink::Storage::Transaction &synchronizationTransaction, const QByteArray &bufferType,
    const std::function<void(const std::function<void(const QByteArray &key)> &callback)> &entryGenerator, std::function<bool(const QByteArray &remoteId)> exists)
{
    entryGenerator([this, &transaction, bufferType, &synchronizationTransaction, &exists](const QByteArray &key) {
        auto sinkId = Sink::Storage::uidFromKey(key);
        Trace() << "Checking for removal " << key;
        const auto remoteId = resolveLocalId(bufferType, sinkId, synchronizationTransaction);
        // If we have no remoteId, the entity hasn't been replayed to the source yet
        if (!remoteId.isEmpty()) {
            if (!exists(remoteId)) {
                Trace() << "Found a removed entity: " << sinkId;
                deleteEntity(sinkId, Sink::Storage::maxRevision(transaction), bufferType,
                    [this](const QByteArray &buffer) { enqueueCommand(mSynchronizerQueue, Sink::Commands::DeleteEntityCommand, buffer); });
            }
        }
    });
}

static QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> getLatest(const Sink::Storage::NamedDatabase &db, const QByteArray &uid, DomainTypeAdaptorFactoryInterface &adaptorFactory)
{
    QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> current;
    db.findLatest(uid,
        [&current, &adaptorFactory](const QByteArray &key, const QByteArray &data) -> bool {
            Sink::EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
            if (!buffer.isValid()) {
                Warning() << "Read invalid buffer from disk";
            } else {
                current = adaptorFactory.createAdaptor(buffer.entity());
            }
            return false;
        },
        [](const Sink::Storage::Error &error) { Warning() << "Failed to read current value from storage: " << error.message; });
    return current;
}

void GenericResource::createOrModify(Sink::Storage::Transaction &transaction, Sink::Storage::Transaction &synchronizationTransaction,
    DomainTypeAdaptorFactoryInterface &adaptorFactory, const QByteArray &bufferType, const QByteArray &remoteId, const Sink::ApplicationDomain::ApplicationDomainType &entity)
{
    auto mainDatabase = Storage::mainDatabase(transaction, bufferType);
    const auto sinkId = resolveRemoteId(bufferType, remoteId, synchronizationTransaction);
    const auto found = mainDatabase.contains(sinkId);
    if (!found) {
        Trace() << "Found a new entity: " << remoteId;
        createEntity(
            sinkId, bufferType, entity, adaptorFactory, [this](const QByteArray &buffer) { enqueueCommand(mSynchronizerQueue, Sink::Commands::CreateEntityCommand, buffer); });
    } else { // modification
        if (auto current = getLatest(mainDatabase, sinkId, adaptorFactory)) {
            bool changed = false;
            for (const auto &property : entity.changedProperties()) {
                if (entity.getProperty(property) != current->getProperty(property)) {
                    Trace() << "Property changed " << sinkId << property;
                    changed = true;
                }
            }
            if (changed) {
                Trace() << "Found a modified entity: " << remoteId;
                modifyEntity(sinkId, Sink::Storage::maxRevision(transaction), bufferType, entity, adaptorFactory,
                    [this](const QByteArray &buffer) { enqueueCommand(mSynchronizerQueue, Sink::Commands::ModifyEntityCommand, buffer); });
            }
        } else {
            Warning() << "Failed to get current entity";
        }
    }
}


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "genericresource.moc"
#pragma clang diagnostic pop
