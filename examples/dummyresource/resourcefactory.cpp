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
#include "domain/event.h"
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


//FIXME We need to pass the resource-instance name to generic resource, not the plugin name
DummyResource::DummyResource()
    : Akonadi2::GenericResource(PLUGIN_NAME ".instance1")
{
}

void DummyResource::configurePipeline(Akonadi2::Pipeline *pipeline)
{
    //TODO In case of a non 1:1 mapping between resource and domain types special handling is required.
    //i.e. If a resource stores tags as part of each message it needs to update the tag index

    auto eventFactory = QSharedPointer<DummyEventAdaptorFactory>::create();
    const auto resourceIdentifier = mResourceInstanceIdentifier;
    auto eventIndexer = new SimpleProcessor("eventIndexer", [eventFactory, resourceIdentifier](const Akonadi2::PipelineState &state, const Akonadi2::Entity &entity) {
        auto adaptor = eventFactory->createAdaptor(entity);
        //FIXME set revision?
        Akonadi2::ApplicationDomain::Event event(resourceIdentifier, state.key(), -1, adaptor);
        Akonadi2::ApplicationDomain::EventImplementation::index(event);
    });

    //event is the entitytype and not the domain type
    pipeline->setPreprocessors("event", Akonadi2::Pipeline::NewPipeline, QVector<Akonadi2::Preprocessor*>() << eventIndexer);
    GenericResource::configurePipeline(pipeline);
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

KAsync::Job<void> DummyResource::synchronizeWithSource(Akonadi2::Pipeline *pipeline)
{
    return KAsync::start<void>([this, pipeline](KAsync::Future<void> &f) {
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
