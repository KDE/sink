/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include "resourcefactory.h"
#include "facade.h"
#include "entitybuffer.h"
#include "pipeline.h"
#include "dummycalendar_generated.h"
#include "metadata_generated.h"
#include "queuedcommand_generated.h"
#include "createentity_generated.h"
#include "domainadaptor.h"
#include "commands.h"
#include "clientapi.h"
#include "index.h"
#include "log.h"
#include <QUuid>
#include <assert.h>


/*
 * Figure out how to implement various classes of processors:
 * * read-only (index and such) => extractor function, probably using domain adaptor
 * * filter => provide means to move entity elsewhere, and also reflect change in source (I guess?)
 * * flag extractors? => like read-only? Or write to local portion of buffer?
 * ** $ISSPAM should become part of domain object and is written to the local part of the mail. 
 * ** => value could be calculated by the server directly
 */
class SimpleProcessor : public Akonadi2::Preprocessor
{
public:
    SimpleProcessor(const QString &id, const std::function<void(const Akonadi2::PipelineState &state, const Akonadi2::Entity &e)> &f)
        : Akonadi2::Preprocessor(),
        mFunction(f),
        mId(id)
    {
    }

    void process(const Akonadi2::PipelineState &state, const Akonadi2::Entity &e) Q_DECL_OVERRIDE
    {
        mFunction(state, e);
        processingCompleted(state);
    }

    QString id() const
    {
        return mId;
    }

protected:
    std::function<void(const Akonadi2::PipelineState &state, const Akonadi2::Entity &e)> mFunction;
    QString mId;
};



static std::string createEvent()
{
    static const size_t attachmentSize = 1024*2; // 2KB
    static uint8_t rawData[attachmentSize];
    static flatbuffers::FlatBufferBuilder fbb;
    fbb.Clear();
    {
        uint8_t *rawDataPtr = Q_NULLPTR;
        auto summary = fbb.CreateString("summary");
        auto data = fbb.CreateUninitializedVector<uint8_t>(attachmentSize, &rawDataPtr);
        DummyCalendar::DummyEventBuilder eventBuilder(fbb);
        eventBuilder.add_summary(summary);
        eventBuilder.add_attachment(data);
        auto eventLocation = eventBuilder.Finish();
        DummyCalendar::FinishDummyEventBuffer(fbb, eventLocation);
        memcpy((void*)rawDataPtr, rawData, attachmentSize);
    }

    return std::string(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
}

QMap<QString, QString> populate()
{
    QMap<QString, QString> content;
    for (int i = 0; i < 2; i++) {
        auto event = createEvent();
        content.insert(QString("key%1").arg(i), QString::fromStdString(event));
    }
    return content;
}

static QMap<QString, QString> s_dataSource = populate();

//Drives the pipeline using the output from all command queues
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

    Async::Job<void> processQueuedCommand(const Akonadi2::QueuedCommand *queuedCommand)
    {
        Log() << "Processing command: " << Akonadi2::Commands::name(queuedCommand->commandId());
        //Throw command into appropriate pipeline
        switch (queuedCommand->commandId()) {
            case Akonadi2::Commands::DeleteEntityCommand:
                //mPipeline->removedEntity
                return Async::null<void>();
            case Akonadi2::Commands::ModifyEntityCommand:
                //mPipeline->modifiedEntity
                return Async::null<void>();
            case Akonadi2::Commands::CreateEntityCommand:
                return mPipeline->newEntity(queuedCommand->command()->Data(), queuedCommand->command()->size());
            default:
                return Async::error<void>(-1, "Unhandled command");
        }
        return Async::null<void>();
    }

    //Process all messages of this queue
    Async::Job<void> processQueue(MessageQueue *queue)
    {
        //TODO use something like:
        //Async::foreach("pass iterator here").each("process value here").join();
        //Async::foreach("pass iterator here").parallel("process value here").join();
        return Async::dowhile(
            [this, queue](Async::Future<bool> &future) {
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
                        //TODO JOBAPI: job lifetime management
                        //Right now we're just leaking jobs. In this case we'd like jobs that are heap allocated and delete
                        //themselves once done. In other cases we'd like jobs that only live as long as their handle though.
                        //FIXME this job is stack allocated and thus simply dies....
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

    Async::Job<void> processPipeline()
    {
        //Go through all message queues
        auto it = QSharedPointer<QListIterator<MessageQueue*> >::create(mCommandQueues);
        return Async::dowhile(
            [it]() { return it->hasNext(); },
            [it, this](Async::Future<void> &future) {
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

DummyResource::DummyResource()
    : Akonadi2::Resource(),
    mUserQueue(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/storage", "org.kde.dummy.userqueue"),
    mSynchronizerQueue(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/storage", "org.kde.dummy.synchronizerqueue"),
    mError(0)
{
}

void DummyResource::configurePipeline(Akonadi2::Pipeline *pipeline)
{
    auto eventFactory = QSharedPointer<DummyEventAdaptorFactory>::create();
    //FIXME we should setup for each resource entity type, not for each domain type
    //i.e. If a resource stores tags as part of each message it needs to update the tag index
    //TODO setup preprocessors for each resource entity type and pipeline type allowing full customization
    //Eventually the order should be self configuring, for now it's hardcoded.
    auto eventIndexer = new SimpleProcessor("summaryprocessor", [eventFactory](const Akonadi2::PipelineState &state, const Akonadi2::Entity &entity) {
        auto adaptor = eventFactory->createAdaptor(entity);
        // Log() << "Summary preprocessor: " << adaptor->getProperty("summary").toString();
    });

    auto uidIndexer = new SimpleProcessor("uidIndexer", [eventFactory](const Akonadi2::PipelineState &state, const Akonadi2::Entity &entity) {
        static Index uidIndex(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/storage", "org.kde.dummy.index.uid", Akonadi2::Storage::ReadWrite);

        //TODO: Benchmark if this is performance wise acceptable, or if we have to access the buffer directly
        auto adaptor = eventFactory->createAdaptor(entity);
        const auto uid = adaptor->getProperty("uid");
        if (uid.isValid()) {
            uidIndex.add(uid.toByteArray(), state.key());
        }
    });

    //event is the entitytype and not the domain type
    pipeline->setPreprocessors("event", Akonadi2::Pipeline::NewPipeline, QVector<Akonadi2::Preprocessor*>() << eventIndexer << uidIndexer);
    mProcessor = new Processor(pipeline, QList<MessageQueue*>() << &mUserQueue << &mSynchronizerQueue);
    QObject::connect(mProcessor, &Processor::error, [this](int errorCode, const QString &msg) { onProcessorError(errorCode, msg); });
}

void DummyResource::onProcessorError(int errorCode, const QString &errorMessage)
{
    Warning() << "Received error from Processor: " << errorCode << errorMessage;
    mError = errorCode;
}

int DummyResource::error() const
{
    return mError;
}

void findByRemoteId(QSharedPointer<Akonadi2::Storage> storage, const QString &rid, std::function<void(void *keyValue, int keySize, void *dataValue, int dataSize)> callback)
{
    //TODO lookup in rid index instead of doing a full scan
    const std::string ridString = rid.toStdString();
    storage->scan("", [&](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
        if (Akonadi2::Storage::isInternalKey(keyValue, keySize)) {
            return true;
        }

        Akonadi2::EntityBuffer::extractResourceBuffer(dataValue, dataSize, [&](const uint8_t *buffer, size_t size) {
            flatbuffers::Verifier verifier(buffer, size);
            if (DummyCalendar::VerifyDummyEventBuffer(verifier)) {
                DummyCalendar::DummyEvent const *resourceBuffer = DummyCalendar::GetDummyEvent(buffer);
                if (resourceBuffer && resourceBuffer->remoteId()) {
                    if (std::string(resourceBuffer->remoteId()->c_str(), resourceBuffer->remoteId()->size()) == ridString) {
                        callback(keyValue, keySize, dataValue, dataSize);
                    }
                }
            }
        });
        return true;
    });
}

void DummyResource::enqueueCommand(MessageQueue &mq, int commandId, const QByteArray &data)
{
    m_fbb.Clear();
    auto commandData = Akonadi2::EntityBuffer::appendAsVector(m_fbb, data.constData(), data.size());
    auto buffer = Akonadi2::CreateQueuedCommand(m_fbb, commandId, commandData);
    Akonadi2::FinishQueuedCommandBuffer(m_fbb, buffer);
    mq.enqueue(m_fbb.GetBufferPointer(), m_fbb.GetSize());
}

Async::Job<void> DummyResource::synchronizeWithSource(Akonadi2::Pipeline *pipeline)
{
    return Async::start<void>([this, pipeline](Async::Future<void> &f) {
        //TODO use a read-only transaction during the complete sync to sync against a defined revision
        auto storage = QSharedPointer<Akonadi2::Storage>::create(Akonadi2::Store::storageLocation(), "org.kde.dummy");
        for (auto it = s_dataSource.constBegin(); it != s_dataSource.constEnd(); it++) {
            bool isNew = true;
            if (storage->exists()) {
                findByRemoteId(storage, it.key(), [&](void *keyValue, int keySize, void *dataValue, int dataSize) {
                    isNew = false;
                });
            }
            if (isNew) {
                m_fbb.Clear();

                const QByteArray data = it.value().toUtf8();
                auto eventBuffer = DummyCalendar::GetDummyEvent(data.data());

                //Map the source format to the buffer format (which happens to be an exact copy here)
                auto summary = m_fbb.CreateString(eventBuffer->summary()->c_str());
                auto rid = m_fbb.CreateString(it.key().toStdString().c_str());
                auto description = m_fbb.CreateString(it.key().toStdString().c_str());
                static uint8_t rawData[100];
                auto attachment = Akonadi2::EntityBuffer::appendAsVector(m_fbb, rawData, 100);

                auto builder = DummyCalendar::DummyEventBuilder(m_fbb);
                builder.add_summary(summary);
                builder.add_remoteId(rid);
                builder.add_description(description);
                builder.add_attachment(attachment);
                auto buffer = builder.Finish();
                DummyCalendar::FinishDummyEventBuffer(m_fbb, buffer);
                flatbuffers::FlatBufferBuilder entityFbb;
                Akonadi2::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, m_fbb.GetBufferPointer(), m_fbb.GetSize(), 0, 0);

                flatbuffers::FlatBufferBuilder fbb;
                //This is the resource type and not the domain type
                auto type = fbb.CreateString("event");
                auto delta = Akonadi2::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
                auto location = Akonadi2::Commands::CreateCreateEntity(fbb, type, delta);
                Akonadi2::Commands::FinishCreateEntityBuffer(fbb, location);

                enqueueCommand(mSynchronizerQueue, Akonadi2::Commands::CreateEntityCommand, QByteArray::fromRawData(reinterpret_cast<char const *>(fbb.GetBufferPointer()), fbb.GetSize()));
            } else { //modification
                //TODO diff and create modification if necessary
            }
        }
        //TODO find items to remove
        f.setFinished();
    });
}

Async::Job<void> DummyResource::processAllMessages()
{
    //We have to wait for all items to be processed to ensure the synced items are available when a query gets executed.
    //TODO: report errors while processing sync?
    //TODO JOBAPI: A helper that waits for n events and then continues?
    return Async::start<void>([this](Async::Future<void> &f) {
        if (mSynchronizerQueue.isEmpty()) {
            f.setFinished();
        } else {
            QObject::connect(&mSynchronizerQueue, &MessageQueue::drained, [&f]() {
                f.setFinished();
            });
        }
    }).then<void>([this](Async::Future<void> &f) {
        if (mUserQueue.isEmpty()) {
            f.setFinished();
        } else {
            QObject::connect(&mUserQueue, &MessageQueue::drained, [&f]() {
                f.setFinished();
            });
        }
    });
}

void DummyResource::processCommand(int commandId, const QByteArray &data, uint size, Akonadi2::Pipeline *pipeline)
{
    //TODO instead of copying the command including the full entity first into the command queue, we could directly
    //create a new revision, only pushing a handle into the commandqueue with the relevant changeset (for changereplay).
    //The problem is that we then require write access from multiple threads (or even processes to avoid sending the full entity over the wire).
    enqueueCommand(mUserQueue, commandId, data);
}

DummyResourceFactory::DummyResourceFactory(QObject *parent)
    : Akonadi2::ResourceFactory(parent)
{

}

Akonadi2::Resource *DummyResourceFactory::createResource()
{
    return new DummyResource();
}

void DummyResourceFactory::registerFacades(Akonadi2::FacadeFactory &factory)
{
    factory.registerFacade<Akonadi2::ApplicationDomain::Event, DummyResourceFacade>(PLUGIN_NAME);
}

#include "resourcefactory.moc"
