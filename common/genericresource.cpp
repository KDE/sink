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
        for (auto queue : mCommandQueues) {
            const bool ret = connect(queue, &MessageQueue::messageReady, this, &Processor::process);
            Q_UNUSED(ret);
        }
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

    KAsync::Job<void> processQueuedCommand(const Akonadi2::QueuedCommand *queuedCommand)
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
                return KAsync::error<void>(-1, "Unhandled command");
        }
        return KAsync::null<void>();
    }

    KAsync::Job<void> processQueuedCommand(const QByteArray &data)
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(data.constData()), data.size());
        if (!Akonadi2::VerifyQueuedCommandBuffer(verifyer)) {
            Warning() << "invalid buffer";
            return KAsync::error<void>(1, "Invalid Buffer");
        }
        auto queuedCommand = Akonadi2::GetQueuedCommand(data.constData());
        const auto commandId = queuedCommand->commandId();
        Trace() << "Dequeued Command: " << Akonadi2::Commands::name(commandId);
        return processQueuedCommand(queuedCommand).then<void>(
            [commandId]() {
                Trace() << "Command pipeline processed: " << Akonadi2::Commands::name(commandId);
            },
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
                queue->dequeueBatch(100, [this](const QByteArray &data) {
                        Trace() << "Got value";
                        return processQueuedCommand(data);
                    }
                ).then<void>([&future, queue](){
                    future.setFinished();
                },
                [&future](int i, QString error) {
                    Warning() << "Error while getting message from messagequeue: " << error;
                    future.setFinished();
                }).exec();
            }
        )).then<void>([this]() {
            mPipeline->commit();
        });
    }

    KAsync::Job<void> processPipeline()
    {
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
};


GenericResource::GenericResource(const QByteArray &resourceInstanceIdentifier, const QSharedPointer<Pipeline> &pipeline)
    : Akonadi2::Resource(),
    mUserQueue(Akonadi2::storageLocation(), resourceInstanceIdentifier + ".userqueue"),
    mSynchronizerQueue(Akonadi2::storageLocation(), resourceInstanceIdentifier + ".synchronizerqueue"),
    mResourceInstanceIdentifier(resourceInstanceIdentifier),
    mPipeline(pipeline ? pipeline : QSharedPointer<Akonadi2::Pipeline>::create(resourceInstanceIdentifier)),
    mError(0)
{
    mProcessor = new Processor(mPipeline.data(), QList<MessageQueue*>() << &mUserQueue << &mSynchronizerQueue);
    QObject::connect(mProcessor, &Processor::error, [this](int errorCode, const QString &msg) { onProcessorError(errorCode, msg); });
    QObject::connect(mPipeline.data(), &Pipeline::revisionUpdated, this, &Resource::revisionUpdated);

    mCommitQueueTimer.setInterval(100);
    mCommitQueueTimer.setSingleShot(true);
    QObject::connect(&mCommitQueueTimer, &QTimer::timeout, &mUserQueue, &MessageQueue::commit);
}

GenericResource::~GenericResource()
{
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
    mUserQueue.startTransaction();
    enqueueCommand(mUserQueue, commandId, data);
    modifications++;
    if (modifications >= 100) {
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
            QObject::connect(&mCommitQueueTimer, &QTimer::timeout, [&f]() {
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

#include "genericresource.moc"
