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
#include <QDate>

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

void DummyResource::createEvent(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb)
{
    //Map the source format to the buffer format (which happens to be an exact copy here)
    auto summary = m_fbb.CreateString(data.value("summary").toString().toStdString());
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

void DummyResource::createMail(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb)
{
    //Map the source format to the buffer format (which happens to be an exact copy here)
    auto subject = m_fbb.CreateString(data.value("subject").toString().toStdString());
    auto sender = m_fbb.CreateString(data.value("sender").toString().toStdString());
    auto senderName = m_fbb.CreateString(data.value("senderName").toString().toStdString());
    auto date = m_fbb.CreateString(data.value("date").toDate().toString().toStdString());
    auto folder = m_fbb.CreateString(std::string("inbox"));

    auto builder = Akonadi2::ApplicationDomain::Buffer::MailBuilder(m_fbb);
    builder.add_subject(subject);
    builder.add_sender(sender);
    builder.add_senderName(senderName);
    builder.add_unread(data.value("unread").toBool());
    builder.add_important(data.value("important").toBool());
    builder.add_date(date);
    builder.add_folder(folder);
    auto buffer = builder.Finish();
    Akonadi2::ApplicationDomain::Buffer::FinishMailBuffer(m_fbb, buffer);
    Akonadi2::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, 0, 0, m_fbb.GetBufferPointer(), m_fbb.GetSize());
}

void DummyResource::synchronize(const QString &bufferType, const QMap<QString, QMap<QString, QVariant> > &data, Akonadi2::Storage::Transaction &transaction, std::function<void(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb)> createEntity)
{
    Index uidIndex("index.uid", transaction);
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
            createEntity(it.key().toUtf8(), it.value(), entityFbb);

            flatbuffers::FlatBufferBuilder fbb;
            //This is the resource type and not the domain type
            auto type = fbb.CreateString(bufferType.toStdString());
            auto delta = Akonadi2::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
            auto location = Akonadi2::Commands::CreateCreateEntity(fbb, type, delta);
            Akonadi2::Commands::FinishCreateEntityBuffer(fbb, location);

            enqueueCommand(mSynchronizerQueue, Akonadi2::Commands::CreateEntityCommand, QByteArray::fromRawData(reinterpret_cast<char const *>(fbb.GetBufferPointer()), fbb.GetSize()));
        } else { //modification
            //TODO diff and create modification if necessary
        }
    }
    //TODO find items to remove
}

KAsync::Job<void> DummyResource::synchronizeWithSource()
{
    return KAsync::start<void>([this](KAsync::Future<void> &f) {
        auto transaction = Akonadi2::Storage(Akonadi2::storageLocation(), mResourceInstanceIdentifier, Akonadi2::Storage::ReadOnly).createTransaction(Akonadi2::Storage::ReadOnly);

        synchronize(ENTITY_TYPE_EVENT, DummyStore::instance().events(), transaction, [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb) {
            createEvent(ridBuffer, data, entityFbb);
        });
        synchronize(ENTITY_TYPE_MAIL, DummyStore::instance().mails(), transaction, [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb) {
            createMail(ridBuffer, data, entityFbb);
        });

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
