#include "genericresource.h"

#include "entitybuffer.h"
#include "pipeline.h"
#include "queuedcommand_generated.h"
#include "createentity_generated.h"
#include "domainadaptor.h"
#include "commands.h"
#include "clientapi.h"
#include "index.h"
#include "log.h"
#include <assert.h>

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
                //mPipeline->removedEntity
                return KAsync::null<void>();
            case Akonadi2::Commands::ModifyEntityCommand:
                //mPipeline->modifiedEntity
                return KAsync::null<void>();
            case Akonadi2::Commands::CreateEntityCommand:
                return mPipeline->newEntity(queuedCommand->command()->Data(), queuedCommand->command()->size());
            default:
                return KAsync::error<void>(-1, "Unhandled command");
        }
        return KAsync::null<void>();
    }

    //Process all messages of this queue
    KAsync::Job<void> processQueue(MessageQueue *queue)
    {
        //TODO use something like:
        //KAsync::foreach("pass iterator here").each("process value here").join();
        //KAsync::foreach("pass iterator here").parallel("process value here").join();
        return KAsync::dowhile(
            [this, queue](KAsync::Future<bool> &future) {
                if (queue->isEmpty()) {
                    future.setValue(false);
                    future.setFinished();
                    return;
                }
                queue->dequeue(
                    [this, &future](void *ptr, int size, std::function<void(bool success)> messageQueueCallback) {
                        auto callback = [messageQueueCallback, &future](bool success) {
                            messageQueueCallback(success);
                            future.setValue(!success);
                            future.setFinished();
                        };

                        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(ptr), size);
                        if (!Akonadi2::VerifyQueuedCommandBuffer(verifyer)) {
                            Warning() << "invalid buffer";
                            callback(false);
                            return;
                        }
                        auto queuedCommand = Akonadi2::GetQueuedCommand(ptr);
                        Trace() << "Dequeued Command: " << Akonadi2::Commands::name(queuedCommand->commandId());
                        processQueuedCommand(queuedCommand).then<void>(
                            [callback]() {
                                callback(true);
                            },
                            [callback](int errorCode, QString errorMessage) {
                                Warning() << "Error while processing queue command: " << errorMessage;
                                callback(false);
                            }
                        ).exec();
                    },
                    [&future](const MessageQueue::Error &error) {
                        Warning() << "Error while getting message from messagequeue: " << error.message;
                        future.setValue(false);
                        future.setFinished();
                    }
                );
            }
        );
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


GenericResource::GenericResource(const QByteArray &resourceInstanceIdentifier)
    : Akonadi2::Resource(),
    mUserQueue(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/storage", resourceInstanceIdentifier + ".userqueue"),
    mSynchronizerQueue(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/storage", resourceInstanceIdentifier + ".synchronizerqueue"),
    mResourceInstanceIdentifier(resourceInstanceIdentifier),
    mError(0)
{
}

GenericResource::~GenericResource()
{

}

void GenericResource::configurePipeline(Akonadi2::Pipeline *pipeline)
{
    //TODO figure out lifetime of the processor
    mProcessor = new Processor(pipeline, QList<MessageQueue*>() << &mUserQueue << &mSynchronizerQueue);
    QObject::connect(mProcessor, &Processor::error, [this](int errorCode, const QString &msg) { onProcessorError(errorCode, msg); });
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

void GenericResource::processCommand(int commandId, const QByteArray &data, uint size, Akonadi2::Pipeline *pipeline)
{
    //TODO instead of copying the command including the full entity first into the command queue, we could directly
    //create a new revision, only pushing a handle into the commandqueue with the relevant changeset (for changereplay).
    //The problem is that we then require write access from multiple threads (or even processes to avoid sending the full entity over the wire).
    enqueueCommand(mUserQueue, commandId, data);
}

KAsync::Job<void> GenericResource::processAllMessages()
{
    //We have to wait for all items to be processed to ensure the synced items are available when a query gets executed.
    //TODO: report errors while processing sync?
    //TODO JOBAPI: A helper that waits for n events and then continues?
    return KAsync::start<void>([this](KAsync::Future<void> &f) {
        if (mSynchronizerQueue.isEmpty()) {
            f.setFinished();
        } else {
            QObject::connect(&mSynchronizerQueue, &MessageQueue::drained, [&f]() {
                f.setFinished();
            });
        }
    }).then<void>([this](KAsync::Future<void> &f) {
        if (mUserQueue.isEmpty()) {
            f.setFinished();
        } else {
            QObject::connect(&mUserQueue, &MessageQueue::drained, [&f]() {
                f.setFinished();
            });
        }
    });
}

#include "genericresource.moc"
