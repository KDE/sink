#include "genericresource.h"

#include "entitybuffer.h"
#include "pipeline.h"
#include "queuedcommand_generated.h"
#include "createentity_generated.h"
#include "domainadaptor.h"
#include "commands.h"
#include "index.h"
#include "log.h"
#include "definitions.h"

using namespace Akonadi2;

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
        : mStorage(storageLocation(), resourceName, Storage::ReadOnly),
        mChangeReplayStore(storageLocation(), resourceName + ".changereplay", Storage::ReadWrite),
        mReplayFunction(replayFunction)
    {

    }

    qint64 getLastReplayedRevision()
    {
        qint64 lastReplayedRevision = 0;
        auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadOnly);
        replayStoreTransaction.openDatabase().scan("lastReplayedRevision", [&lastReplayedRevision](const QByteArray &key, const QByteArray &value) -> bool {
            lastReplayedRevision = value.toLongLong();
            return false;
        }, [](const Storage::Error &) {
        });
        return lastReplayedRevision;
    }

Q_SIGNALS:
    void changesReplayed();

public Q_SLOTS:
    void revisionChanged()
    {
        auto mainStoreTransaction = mStorage.createTransaction(Storage::ReadOnly);
        auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadWrite);
        qint64 lastReplayedRevision = 1;
        replayStoreTransaction.openDatabase().scan("lastReplayedRevision", [&lastReplayedRevision](const QByteArray &key, const QByteArray &value) -> bool {
            lastReplayedRevision = value.toLongLong();
            return false;
        }, [](const Storage::Error &) {
        });
        const qint64 topRevision = Storage::maxRevision(mainStoreTransaction);

        if (lastReplayedRevision < topRevision) {
            qint64 revision = lastReplayedRevision;
            for (;revision <= topRevision; revision++) {
                const auto uid = Storage::getUidFromRevision(mainStoreTransaction, revision);
                const auto type = Storage::getTypeFromRevision(mainStoreTransaction, revision);
                const auto key = Storage::assembleKey(uid, revision);
                mainStoreTransaction.openDatabase(type + ".main").scan(key, [&lastReplayedRevision, type, this](const QByteArray &key, const QByteArray &value) -> bool {
                    mReplayFunction(type, key, value).exec();
                    //TODO make for loop async, and pass to async replay function together with type
                    Trace() << "Replaying " << key;
                    return false;
                }, [key](const Storage::Error &) {
                    ErrorMsg() << "Failed to replay change " << key;
                });
            }
            revision--;
            replayStoreTransaction.openDatabase().write("lastReplayedRevision", QByteArray::number(revision));
            replayStoreTransaction.commit();
            Trace() << "Replayed until " << revision;
        }
    }

private:
    Akonadi2::Storage mStorage;
    Akonadi2::Storage mChangeReplayStore;
    ReplayFunction mReplayFunction;
};

/**
 * Drives the pipeline using the output from all command queues
 */
class Processor : public QObject
{
    Q_OBJECT
public:
    Processor(Akonadi2::Pipeline *pipeline, QList<MessageQueue*> commandQueues)
        : QObject(),
        mPipeline(pipeline),
        mCommandQueues(commandQueues),
        mProcessingLock(false)
    {
        mPipeline->startTransaction();
        //FIXME Should be initialized to the current value of the change replay queue
        mLowerBoundRevision = Storage::maxRevision(mPipeline->transaction());
        mPipeline->commit();

        for (auto queue : mCommandQueues) {
            const bool ret = connect(queue, &MessageQueue::messageReady, this, &Processor::process);
            Q_UNUSED(ret);
        }
    }

    void setOldestUsedRevision(qint64 revision)
    {
        mLowerBoundRevision = revision;
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
        auto job = processPipeline().then<void>([this]() {
            mProcessingLock = false;
            if (messagesToProcessAvailable()) {
                process();
            }
        }).exec();
    }

    KAsync::Job<qint64> processQueuedCommand(const Akonadi2::QueuedCommand *queuedCommand)
    {
        Log() << "Processing command: " << Akonadi2::Commands::name(queuedCommand->commandId());
        //Throw command into appropriate pipeline
        switch (queuedCommand->commandId()) {
            case Akonadi2::Commands::DeleteEntityCommand:
                return mPipeline->deletedEntity(queuedCommand->command()->Data(), queuedCommand->command()->size());
            case Akonadi2::Commands::ModifyEntityCommand:
                return mPipeline->modifiedEntity(queuedCommand->command()->Data(), queuedCommand->command()->size());
            case Akonadi2::Commands::CreateEntityCommand:
                return mPipeline->newEntity(queuedCommand->command()->Data(), queuedCommand->command()->size());
            default:
                return KAsync::error<qint64>(-1, "Unhandled command");
        }
        return KAsync::null<qint64>();
    }

    KAsync::Job<qint64, qint64> processQueuedCommand(const QByteArray &data)
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(data.constData()), data.size());
        if (!Akonadi2::VerifyQueuedCommandBuffer(verifyer)) {
            Warning() << "invalid buffer";
            // return KAsync::error<void, qint64>(1, "Invalid Buffer");
        }
        auto queuedCommand = Akonadi2::GetQueuedCommand(data.constData());
        const auto commandId = queuedCommand->commandId();
        Trace() << "Dequeued Command: " << Akonadi2::Commands::name(commandId);
        return processQueuedCommand(queuedCommand).then<qint64, qint64>(
            [commandId](qint64 createdRevision) -> qint64 {
                Trace() << "Command pipeline processed: " << Akonadi2::Commands::name(commandId);
                return createdRevision;
            }
            ,
            [](int errorCode, QString errorMessage) {
                //FIXME propagate error, we didn't handle it
                Warning() << "Error while processing queue command: " << errorMessage;
            }
        );
    }

    //Process all messages of this queue
    KAsync::Job<void> processQueue(MessageQueue *queue)
    {
        return KAsync::start<void>([this](){
            mPipeline->startTransaction();
        }).then(KAsync::dowhile(
            [queue]() { return !queue->isEmpty(); },
            [this, queue](KAsync::Future<void> &future) {
                const int batchSize = 100;
                queue->dequeueBatch(batchSize, [this](const QByteArray &data) {
                        return KAsync::start<void>([this, data](KAsync::Future<void> &future) {
                            processQueuedCommand(data).then<void, qint64>([&future, this](qint64 createdRevision) {
                                Trace() << "Created revision " << createdRevision;
                                future.setFinished();
                            }).exec();
                        });
                    }
                ).then<void>([&future, queue](){
                    future.setFinished();
                },
                [&future](int i, QString error) {
                    if (i != MessageQueue::ErrorCodes::NoMessageFound) {
                        Warning() << "Error while getting message from messagequeue: " << error;
                    }
                    future.setFinished();
                }).exec();
            }
        )).then<void>([this]() {
            mPipeline->commit();
        });
    }

    KAsync::Job<void> processPipeline()
    {
        mPipeline->startTransaction();
        Trace() << "Cleaning up from " << mPipeline->cleanedUpRevision() + 1 << " to " << mLowerBoundRevision;
        for (qint64 revision = mPipeline->cleanedUpRevision() + 1; revision <= mLowerBoundRevision; revision++) {
            mPipeline->cleanupRevision(revision);
        }
        mPipeline->commit();

        //Go through all message queues
        auto it = QSharedPointer<QListIterator<MessageQueue*> >::create(mCommandQueues);
        return KAsync::dowhile(
            [it]() { return it->hasNext(); },
            [it, this](KAsync::Future<void> &future) {
                auto queue = it->next();
                processQueue(queue).then<void>([&future]() {
                    Trace() << "Queue processed";
                    future.setFinished();
                }).exec();
            }
        );
    }

private:
    Akonadi2::Pipeline *mPipeline;
    //Ordered by priority
    QList<MessageQueue*> mCommandQueues;
    bool mProcessingLock;
    //The lowest revision we no longer need
    qint64 mLowerBoundRevision;
};


GenericResource::GenericResource(const QByteArray &resourceInstanceIdentifier, const QSharedPointer<Pipeline> &pipeline)
    : Akonadi2::Resource(),
    mUserQueue(Akonadi2::storageLocation(), resourceInstanceIdentifier + ".userqueue"),
    mSynchronizerQueue(Akonadi2::storageLocation(), resourceInstanceIdentifier + ".synchronizerqueue"),
    mResourceInstanceIdentifier(resourceInstanceIdentifier),
    mPipeline(pipeline ? pipeline : QSharedPointer<Akonadi2::Pipeline>::create(resourceInstanceIdentifier)),
    mError(0),
    mClientLowerBoundRevision(std::numeric_limits<qint64>::max())
{
    mProcessor = new Processor(mPipeline.data(), QList<MessageQueue*>() << &mUserQueue << &mSynchronizerQueue);
    QObject::connect(mProcessor, &Processor::error, [this](int errorCode, const QString &msg) { onProcessorError(errorCode, msg); });
    QObject::connect(mPipeline.data(), &Pipeline::revisionUpdated, this, &Resource::revisionUpdated);
    mSourceChangeReplay = new ChangeReplay(resourceInstanceIdentifier, [this](const QByteArray &type, const QByteArray &key, const QByteArray &value) {
        return this->replay(type, key, value);
    });
    QObject::connect(mPipeline.data(), &Pipeline::revisionUpdated, mSourceChangeReplay, &ChangeReplay::revisionChanged);
    QObject::connect(mSourceChangeReplay, &ChangeReplay::changesReplayed, this, &GenericResource::updateLowerBoundRevision);
    mClientLowerBoundRevision = mPipeline->cleanedUpRevision();
    mProcessor->setOldestUsedRevision(mSourceChangeReplay->getLastReplayedRevision());

    mCommitQueueTimer.setInterval(100);
    mCommitQueueTimer.setSingleShot(true);
    QObject::connect(&mCommitQueueTimer, &QTimer::timeout, &mUserQueue, &MessageQueue::commit);
}

GenericResource::~GenericResource()
{
    delete mProcessor;
    delete mSourceChangeReplay;
}

KAsync::Job<void> GenericResource::replay(const QByteArray &type, const QByteArray &key, const QByteArray &value)
{
    return KAsync::null<void>();
}

void GenericResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    Akonadi2::Storage(Akonadi2::storageLocation(), instanceIdentifier, Akonadi2::Storage::ReadWrite).removeFromDisk();
    Akonadi2::Storage(Akonadi2::storageLocation(), instanceIdentifier + ".userqueue", Akonadi2::Storage::ReadWrite).removeFromDisk();
    Akonadi2::Storage(Akonadi2::storageLocation(), instanceIdentifier + ".synchronizerqueue", Akonadi2::Storage::ReadWrite).removeFromDisk();
    Akonadi2::Storage(Akonadi2::storageLocation(), instanceIdentifier + ".changereplay", Akonadi2::Storage::ReadWrite).removeFromDisk();
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
    //TODO get rid of m_fbb member variable
    m_fbb.Clear();
    auto commandData = Akonadi2::EntityBuffer::appendAsVector(m_fbb, data.constData(), data.size());
    auto buffer = Akonadi2::CreateQueuedCommand(m_fbb, commandId, commandData);
    Akonadi2::FinishQueuedCommandBuffer(m_fbb, buffer);
    mq.enqueue(m_fbb.GetBufferPointer(), m_fbb.GetSize());
}

void GenericResource::processCommand(int commandId, const QByteArray &data)
{
    static int modifications = 0;
    const int batchSize = 100;
    mUserQueue.startTransaction();
    enqueueCommand(mUserQueue, commandId, data);
    modifications++;
    if (modifications >= batchSize) {
        mUserQueue.commit();
        modifications = 0;
        mCommitQueueTimer.stop();
    } else {
        mCommitQueueTimer.start();
    }
}

static void waitForDrained(KAsync::Future<void> &f, MessageQueue &queue)
{
    if (queue.isEmpty()) {
        f.setFinished();
    } else {
        QObject::connect(&queue, &MessageQueue::drained, [&f]() {
            f.setFinished();
        });
    }
};

KAsync::Job<void> GenericResource::processAllMessages()
{
    //We have to wait for all items to be processed to ensure the synced items are available when a query gets executed.
    //TODO: report errors while processing sync?
    //TODO JOBAPI: A helper that waits for n events and then continues?
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
    }).then<void>([this](KAsync::Future<void> &f) {
        waitForDrained(f, mSynchronizerQueue);
    }).then<void>([this](KAsync::Future<void> &f) {
        waitForDrained(f, mUserQueue);
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

#include "genericresource.moc"
