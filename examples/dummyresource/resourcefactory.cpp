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
#include "mail_generated.h"
#include "queuedcommand_generated.h"
#include "createentity_generated.h"
#include "domainadaptor.h"
#include "commands.h"
#include "index.h"
#include "log.h"
#include "domain/event.h"
#include "domain/mail.h"
#include "dummystore.h"
#include "definitions.h"
#include "facadefactory.h"

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_EVENT "event"
#define ENTITY_TYPE_MAIL "mail"

DummyResource::DummyResource(const QByteArray &instanceIdentifier, const QSharedPointer<Akonadi2::Pipeline> &pipeline)
    : Akonadi2::GenericResource(instanceIdentifier, pipeline)
{
    auto eventFactory = QSharedPointer<DummyEventAdaptorFactory>::create();
    const auto resourceIdentifier = mResourceInstanceIdentifier;

    auto eventIndexer = new Akonadi2::SimpleProcessor("eventIndexer", [eventFactory, resourceIdentifier](const Akonadi2::PipelineState &state, const Akonadi2::Entity &entity, Akonadi2::Storage::Transaction &transaction) {
        auto adaptor = eventFactory->createAdaptor(entity);
        Akonadi2::ApplicationDomain::Event event(resourceIdentifier, state.key(), -1, adaptor);
        Akonadi2::ApplicationDomain::TypeImplementation<Akonadi2::ApplicationDomain::Event>::index(event, transaction);

        Index ridIndex("index.rid", transaction);
        const auto rid = event.getProperty("remoteId");
        if (rid.isValid()) {
            ridIndex.add(rid.toByteArray(), event.identifier());
        }
    });

    mPipeline->setPreprocessors(ENTITY_TYPE_EVENT, Akonadi2::Pipeline::NewPipeline, QVector<Akonadi2::Preprocessor*>() << eventIndexer);
    mPipeline->setAdaptorFactory(ENTITY_TYPE_EVENT, eventFactory);
    //TODO cleanup indexes during removal

    {
        auto mailFactory = QSharedPointer<DummyMailAdaptorFactory>::create();
        auto mailIndexer = new Akonadi2::SimpleProcessor("mailIndexer", [mailFactory, resourceIdentifier](const Akonadi2::PipelineState &state, const Akonadi2::Entity &entity, Akonadi2::Storage::Transaction &transaction) {
            auto adaptor = mailFactory->createAdaptor(entity);
            Akonadi2::ApplicationDomain::Mail mail(resourceIdentifier, state.key(), -1, adaptor);
            Akonadi2::ApplicationDomain::TypeImplementation<Akonadi2::ApplicationDomain::Mail>::index(mail, transaction);

            Index ridIndex("mail.index.rid", transaction);
            const auto rid = mail.getProperty("remoteId");
            if (rid.isValid()) {
                ridIndex.add(rid.toByteArray(), mail.identifier());
            }
        });

        mPipeline->setPreprocessors(ENTITY_TYPE_MAIL, Akonadi2::Pipeline::NewPipeline, QVector<Akonadi2::Preprocessor*>() << mailIndexer);
        mPipeline->setAdaptorFactory(ENTITY_TYPE_MAIL, mailFactory);
    }
}

void DummyResource::createEvent(const QByteArray &ridBuffer, const QByteArray &data, flatbuffers::FlatBufferBuilder &entityFbb)
{
    auto eventBuffer = DummyCalendar::GetDummyEvent(data.data());

    //Map the source format to the buffer format (which happens to be an exact copy here)
    auto summary = m_fbb.CreateString(eventBuffer->summary()->c_str());
    auto rid = m_fbb.CreateString(std::string(ridBuffer.constData(), ridBuffer.size()));
    auto description = m_fbb.CreateString(std::string(ridBuffer.constData(), ridBuffer.size()));
    static uint8_t rawData[100];
    auto attachment = Akonadi2::EntityBuffer::appendAsVector(m_fbb, rawData, 100);

    auto builder = DummyCalendar::DummyEventBuilder(m_fbb);
    builder.add_summary(summary);
    builder.add_remoteId(rid);
    builder.add_description(description);
    builder.add_attachment(attachment);
    auto buffer = builder.Finish();
    DummyCalendar::FinishDummyEventBuffer(m_fbb, buffer);
    Akonadi2::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, m_fbb.GetBufferPointer(), m_fbb.GetSize(), 0, 0);
}

void DummyResource::createMail(const QByteArray &ridBuffer, const QByteArray &data, flatbuffers::FlatBufferBuilder &entityFbb)
{
    auto mailBuffer = Akonadi2::ApplicationDomain::Buffer::GetMail(data.data());

    //Map the source format to the buffer format (which happens to be an exact copy here)
    auto subject = m_fbb.CreateString(mailBuffer->subject()->c_str());
    auto rid = m_fbb.CreateString(std::string(ridBuffer.constData(), ridBuffer.size()));
    // auto description = m_fbb.CreateString(std::string(ridBuffer.constData(), ridBuffer.size()));
    // static uint8_t rawData[100];
    // auto attachment = Akonadi2::EntityBuffer::appendAsVector(m_fbb, rawData, 100);

    auto builder = Akonadi2::ApplicationDomain::Buffer::MailBuilder(m_fbb);
    builder.add_subject(subject);
    // builder.add(rid);
    // builder.add_description(description);
    // builder.add_attachment(attachment);
    auto buffer = builder.Finish();
    Akonadi2::ApplicationDomain::Buffer::FinishMailBuffer(m_fbb, buffer);
    Akonadi2::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, 0, 0, m_fbb.GetBufferPointer(), m_fbb.GetSize());
}

KAsync::Job<void> DummyResource::synchronizeWithSource()
{
    return KAsync::start<void>([this](KAsync::Future<void> &f) {
        auto transaction = Akonadi2::Storage(Akonadi2::storageLocation(), mResourceInstanceIdentifier, Akonadi2::Storage::ReadOnly).createTransaction(Akonadi2::Storage::ReadOnly);
        Index uidIndex("index.uid", transaction);

        const auto data = DummyStore::instance().events();
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

                flatbuffers::FlatBufferBuilder entityFbb;
                createEvent(it.key().toUtf8(), it.value().toUtf8(), entityFbb);

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
 
        const auto mails = DummyStore::instance().mails();
        for (auto it = mails.constBegin(); it != mails.constEnd(); it++) {
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

                flatbuffers::FlatBufferBuilder entityFbb;
                createMail(it.key().toUtf8(), it.value().toUtf8(), entityFbb);

                flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(entityFbb.GetBufferPointer()), entityFbb.GetSize());
                if (!Akonadi2::ApplicationDomain::Buffer::VerifyMailBuffer(verifyer)) {
                    Warning() << "invalid buffer, not a mail buffer";
                }

                flatbuffers::FlatBufferBuilder fbb;
                //This is the resource type and not the domain type
                auto type = fbb.CreateString(ENTITY_TYPE_MAIL);
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
    factory.registerFacade<Akonadi2::ApplicationDomain::Mail, DummyResourceMailFacade>(PLUGIN_NAME);
}

#include "resourcefactory.moc"
