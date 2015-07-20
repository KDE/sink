/*
 *   Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *   Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include "queuedcommand_generated.h"
#include "createentity_generated.h"
#include "domainadaptor.h"
#include "commands.h"
#include "index.h"
#include "log.h"
#include "domain/event.h"
#include "dummystore.h"

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_EVENT "event"

DummyResource::DummyResource(const QByteArray &instanceIdentifier)
    : Akonadi2::GenericResource(instanceIdentifier)
{
}

void DummyResource::configurePipeline(Akonadi2::Pipeline *pipeline)
{
    auto eventFactory = QSharedPointer<DummyEventAdaptorFactory>::create();
    const auto resourceIdentifier = mResourceInstanceIdentifier;
    auto eventIndexer = new Akonadi2::SimpleProcessor("eventIndexer", [eventFactory, resourceIdentifier](const Akonadi2::PipelineState &state, const Akonadi2::Entity &entity) {
        auto adaptor = eventFactory->createAdaptor(entity);
        //FIXME set revision?
        Akonadi2::ApplicationDomain::Event event(resourceIdentifier, state.key(), -1, adaptor);
        Akonadi2::ApplicationDomain::TypeImplementation<Akonadi2::ApplicationDomain::Event>::index(event);

        Index ridIndex(Akonadi2::Store::storageLocation(), resourceIdentifier + ".index.rid", Akonadi2::Storage::ReadWrite);
        const auto rid = event.getProperty("remoteId");
        if (rid.isValid()) {
            ridIndex.add(rid.toByteArray(), event.identifier());
        }
    });

    pipeline->setPreprocessors(ENTITY_TYPE_EVENT, Akonadi2::Pipeline::NewPipeline, QVector<Akonadi2::Preprocessor*>() << eventIndexer);
    pipeline->setAdaptorFactory(ENTITY_TYPE_EVENT, eventFactory);
    //TODO cleanup indexes during removal
    GenericResource::configurePipeline(pipeline);
}

KAsync::Job<void> DummyResource::synchronizeWithSource(Akonadi2::Pipeline *pipeline)
{
    return KAsync::start<void>([this, pipeline](KAsync::Future<void> &f) {
        //TODO start transaction on index
        Index uidIndex(Akonadi2::Store::storageLocation(), mResourceInstanceIdentifier + ".index.uid", Akonadi2::Storage::ReadOnly);

        const auto data = DummyStore::instance().data();
        for (auto it = data.constBegin(); it != data.constEnd(); it++) {
            bool isNew = true;
            uidIndex.lookup(it.key().toLatin1(), [&](const QByteArray &value) {
                isNew = false;
            },
            [](const Index::Error &error) {
                if (error.code != Index::IndexNotAvailable) {
                    Warning() << "Error in uid index: " <<  error.message;
                }
            });
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
                auto type = fbb.CreateString(ENTITY_TYPE_EVENT);
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

Akonadi2::Resource *DummyResourceFactory::createResource(const QByteArray &instanceIdentifier)
{
    return new DummyResource(instanceIdentifier);
}

void DummyResourceFactory::registerFacades(Akonadi2::FacadeFactory &factory)
{
    factory.registerFacade<Akonadi2::ApplicationDomain::Event, DummyResourceFacade>(PLUGIN_NAME);
}

#include "resourcefactory.moc"
