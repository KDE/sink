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
#include "domainadaptor.h"
#include "commands.h"
#include "clientapi.h"
#include <QUuid>

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
    SimpleProcessor(const std::function<void(const Akonadi2::PipelineState &state, const Akonadi2::Entity &e)> &f)
        : Akonadi2::Preprocessor(),
        mFunction(f)
    {
    }

    void process(const Akonadi2::PipelineState &state, const Akonadi2::Entity &e) {
        mFunction(state, e);
        processingCompleted(state);
    }

protected:
    std::function<void(const Akonadi2::PipelineState &state, const Akonadi2::Entity &e)> mFunction;
};

// template <typename DomainType>
// class SimpleReadOnlyProcessor : public SimpleProcessor<DomainType>
// {
// public:
//     using SimpleProcessor::SimpleProcessor;
//     void process(Akonadi2::PipelineState state) {
//         mFunction();
//     }
// };


static std::string createEvent()
{
    static const size_t attachmentSize = 1024*2; // 2KB
    static uint8_t rawData[attachmentSize];
    static flatbuffers::FlatBufferBuilder fbb;
    fbb.Clear();
    {
        auto summary = fbb.CreateString("summary");
        auto data = fbb.CreateUninitializedVector<uint8_t>(attachmentSize);
        DummyCalendar::DummyEventBuilder eventBuilder(fbb);
        eventBuilder.add_summary(summary);
        eventBuilder.add_attachment(data);
        auto eventLocation = eventBuilder.Finish();
        DummyCalendar::FinishDummyEventBuffer(fbb, eventLocation);
        memcpy((void*)DummyCalendar::GetDummyEvent(fbb.GetBufferPointer())->attachment()->Data(), rawData, attachmentSize);
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
            bool ret = connect(queue, &MessageQueue::messageReady, this, &Processor::process);
            Q_ASSERT(ret);
        }
    }

private slots:
    static void asyncWhile(const std::function<void(std::function<void(bool)>)> &body, const std::function<void()> &completionHandler) {
        body([body, completionHandler](bool complete) {
            if (complete) {
                completionHandler();
            } else {
                asyncWhile(body, completionHandler);
            }
        });
    }

    void process()
    {
        if (mProcessingLock) {
            return;
        }
        mProcessingLock = true;
        auto job = processPipeline().then<void>([this](Async::Future<void> &future) {
            mProcessingLock = false;
            future.setFinished();
        }).exec();
    }

    Async::Job<void> processPipeline()
    {
        auto job = Async::start<void>([this](Async::Future<void> &future) {
            //TODO process all queues in async for
            auto queue = mCommandQueues.first();
            asyncWhile([&](std::function<void(bool)> whileCallback) {
                queue->dequeue([this, whileCallback](void *ptr, int size, std::function<void(bool success)> messageQueueCallback) {
                    flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(ptr), size);
                    if (!Akonadi2::VerifyQueuedCommandBuffer(verifyer)) {
                        qWarning() << "invalid buffer";
                        messageQueueCallback(false);
                        whileCallback(true);
                        return;
                    }
                    auto queuedCommand = Akonadi2::GetQueuedCommand(ptr);
                    qDebug() << "Dequeued: " << queuedCommand->commandId();
                    //Throw command into appropriate pipeline
                    switch (queuedCommand->commandId()) {
                        case Akonadi2::Commands::DeleteEntityCommand:
                            //mPipeline->removedEntity
                            break;
                        case Akonadi2::Commands::ModifyEntityCommand:
                            //mPipeline->modifiedEntity
                            break;
                        case Akonadi2::Commands::CreateEntityCommand: {
                            //TODO job lifetime management
                            mPipeline->newEntity(queuedCommand->command()->Data(), queuedCommand->command()->size()).then<void>([&messageQueueCallback, whileCallback](Async::Future<void> &future) {
                                messageQueueCallback(true);
                                whileCallback(false);
                                future.setFinished();
                            }).exec();
                        }
                            break;
                        default:
                            //Unhandled command
                            qWarning() << "Unhandled command";
                            messageQueueCallback(true);
                            whileCallback(false);
                            break;
                    }
                },
                [whileCallback](const MessageQueue::Error &error) {
                    qDebug() << "no more messages in queue";
                    whileCallback(true);
                });
            },
            [&future]() { //while complete
                future.setFinished();
                //Call async-for completion handler
            });
        });
        return job;
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
    mSynchronizerQueue(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/storage", "org.kde.dummy.synchronizerqueue")
{
}

void DummyResource::configurePipeline(Akonadi2::Pipeline *pipeline)
{
    auto eventFactory = QSharedPointer<DummyEventAdaptorFactory>::create();
    //FIXME we should setup for each resource entity type, not for each domain type
    //i.e. If a resource stores tags as part of each message it needs to update the tag index
    //TODO setup preprocessors for each domain type and pipeline type allowing full customization
    //Eventually the order should be self configuring, for now it's hardcoded.
    auto eventIndexer = new SimpleProcessor([eventFactory](const Akonadi2::PipelineState &state, const Akonadi2::Entity &entity) {
        auto adaptor = eventFactory->createAdaptor(entity);
        qDebug() << "Summary preprocessor: " << adaptor->getProperty("summary").toString();
    });
    //event is the entitytype and not the domain type
    pipeline->setPreprocessors("event", Akonadi2::Pipeline::NewPipeline, QVector<Akonadi2::Preprocessor*>() << eventIndexer);
    mProcessor = new Processor(pipeline, QList<MessageQueue*>() << &mUserQueue << &mSynchronizerQueue);
}

void findByRemoteId(QSharedPointer<Akonadi2::Storage> storage, const QString &rid, std::function<void(void *keyValue, int keySize, void *dataValue, int dataSize)> callback)
{
    //TODO lookup in rid index instead of doing a full scan
    const std::string ridString = rid.toStdString();
    storage->scan("", [&](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
        if (QByteArray::fromRawData(static_cast<char*>(keyValue), keySize).startsWith("__internal")) {
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
    auto commandData = m_fbb.CreateVector(reinterpret_cast<uint8_t const *>(data.data()), data.size());
    auto builder = Akonadi2::QueuedCommandBuilder(m_fbb);
    builder.add_commandId(commandId);
    builder.add_command(commandData);
    auto buffer = builder.Finish();
    Akonadi2::FinishQueuedCommandBuffer(m_fbb, buffer);
    mq.enqueue(m_fbb.GetBufferPointer(), m_fbb.GetSize());
}

Async::Job<void> DummyResource::synchronizeWithSource(Akonadi2::Pipeline *pipeline)
{
    qDebug() << "synchronizeWithSource";
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
                auto attachment = m_fbb.CreateVector(rawData, 100);

                auto builder = DummyCalendar::DummyEventBuilder(m_fbb);
                builder.add_summary(summary);
                builder.add_remoteId(rid);
                builder.add_description(description);
                builder.add_attachment(attachment);
                auto buffer = builder.Finish();
                DummyCalendar::FinishDummyEventBuffer(m_fbb, buffer);
                enqueueCommand(mSynchronizerQueue, Akonadi2::Commands::CreateEntityCommand, QByteArray::fromRawData(reinterpret_cast<char const *>(m_fbb.GetBufferPointer()), m_fbb.GetSize()));
            } else { //modification
                //TODO diff and create modification if necessary
            }
        }
        //TODO find items to remove
        f.setFinished();
    });
}

void DummyResource::processCommand(int commandId, const QByteArray &data, uint size, Akonadi2::Pipeline *pipeline)
{
    qDebug() << "processCommand";
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
    factory.registerFacade<Akonadi2::Domain::Event, DummyResourceFacade>(PLUGIN_NAME);
}

#include "resourcefactory.moc"
