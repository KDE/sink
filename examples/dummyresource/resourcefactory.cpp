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
#include "notification_generated.h"
#include "mail_generated.h"
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
#include "adaptorfactoryregistry.h"
#include "synchronizer.h"
#include "mailpreprocessor.h"
#include "remoteidmap.h"
#include <QDate>
#include <QUuid>

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_EVENT "event"
#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

SINK_DEBUG_AREA("dummyresource")

class DummySynchronizer : public Sink::Synchronizer {
    public:

    DummySynchronizer(const Sink::ResourceContext &context)
        : Sink::Synchronizer(context)
    {

    }

    Sink::ApplicationDomain::Event::Ptr createEvent(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data)
    {
        static uint8_t rawData[100];
        auto event = Sink::ApplicationDomain::Event::Ptr::create();
        event->setProperty("summary", data.value("summary").toString());
        event->setProperty("remoteId", ridBuffer);
        event->setProperty("description", data.value("description").toString());
        event->setProperty("attachment", QByteArray::fromRawData(reinterpret_cast<const char*>(rawData), 100));
        return event;
    }

    Sink::ApplicationDomain::Mail::Ptr createMail(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data)
    {
        auto mail = Sink::ApplicationDomain::Mail::Ptr::create();
        mail->setProperty("subject", data.value("subject").toString());
        mail->setProperty("senderEmail", data.value("senderEmail").toString());
        mail->setProperty("senderName", data.value("senderName").toString());
        mail->setProperty("date", data.value("date").toString());
        mail->setProperty("folder", syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, data.value("parentFolder").toByteArray()));
        mail->setProperty("unread", data.value("unread").toBool());
        mail->setProperty("important", data.value("important").toBool());
        return mail;
    }

    Sink::ApplicationDomain::Folder::Ptr createFolder(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data)
    {
        auto folder = Sink::ApplicationDomain::Folder::Ptr::create();
        folder->setProperty("name", data.value("name").toString());
        folder->setProperty("icon", data.value("icon").toString());
        if (!data.value("parent").toString().isEmpty()) {
            auto sinkId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, data.value("parent").toByteArray());
            folder->setProperty("parent", sinkId);
        }
        return folder;
    }

    void synchronize(const QByteArray &bufferType, const QMap<QString, QMap<QString, QVariant> > &data, std::function<Sink::ApplicationDomain::ApplicationDomainType::Ptr(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data)> createEntity)
    {
        auto time = QSharedPointer<QTime>::create();
        time->start();
        //TODO find items to remove
        int count = 0;
        for (auto it = data.constBegin(); it != data.constEnd(); it++) {
            count++;
            const auto remoteId = it.key().toUtf8();
            auto entity = createEntity(remoteId, it.value());
            createOrModify(bufferType, remoteId, *entity);
        }
        SinkTrace() << "Sync of " << count << " entities of type " << bufferType << " done." << Sink::Log::TraceTime(time->elapsed());
    }

    KAsync::Job<void> synchronizeWithSource(const Sink::QueryBase &) Q_DECL_OVERRIDE
    {
        SinkLog() << " Synchronizing with the source";
        return KAsync::syncStart<void>([this]() {
            synchronize(ENTITY_TYPE_EVENT, DummyStore::instance().events(), [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data) {
                return createEvent(ridBuffer, data);
            });
            synchronize(ENTITY_TYPE_MAIL, DummyStore::instance().mails(), [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data) {
                return createMail(ridBuffer, data);
            });
            synchronize(ENTITY_TYPE_FOLDER, DummyStore::instance().folders(), [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data) {
                return createFolder(ridBuffer, data);
            });
            SinkLog() << "Done Synchronizing";
        });
    }

};

DummyResource::DummyResource(const Sink::ResourceContext &resourceContext, const QSharedPointer<Sink::Pipeline> &pipeline)
    : Sink::GenericResource(resourceContext, pipeline)
{
    setupSynchronizer(QSharedPointer<DummySynchronizer>::create(resourceContext));
    setupChangereplay(QSharedPointer<Sink::NullChangeReplay>::create(resourceContext));
    setupPreprocessors(ENTITY_TYPE_MAIL,
            QVector<Sink::Preprocessor*>() << new MailPropertyExtractor);
    setupPreprocessors(ENTITY_TYPE_FOLDER,
            QVector<Sink::Preprocessor*>());
    setupPreprocessors(ENTITY_TYPE_EVENT,
            QVector<Sink::Preprocessor*>());
}

DummyResource::~DummyResource()
{

}

KAsync::Job<void> DummyResource::synchronizeWithSource(const Sink::QueryBase &query)
{
    SinkTrace() << "Synchronize with source and sending a notification about it";
    Sink::Notification n;
    n.id = "connected";
    n.type = Sink::Notification::Status;
    n.message = "We're connected";
    n.code = Sink::ApplicationDomain::ConnectedStatus;
    emit notify(n);
    return GenericResource::synchronizeWithSource(query);
}

KAsync::Job<void> DummyResource::inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
{

    SinkTrace() << "Inspecting " << inspectionType << domainType << entityId << property << expectedValue;
    if (property == "testInspection") {
        if (expectedValue.toBool()) {
            //Success
            return KAsync::null<void>();
        } else {
            //Failure
            return KAsync::error<void>(1, "Failed.");
        }
    }
    return KAsync::null<void>();
}


DummyResourceFactory::DummyResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent)
{

}

Sink::Resource *DummyResourceFactory::createResource(const Sink::ResourceContext &resourceContext)
{
    return new DummyResource(resourceContext);
}

void DummyResourceFactory::registerFacades(const QByteArray &resourceName, Sink::FacadeFactory &factory)
{
    factory.registerFacade<Sink::ApplicationDomain::Event, DummyResourceFacade>(resourceName);
    factory.registerFacade<Sink::ApplicationDomain::Mail, DummyResourceMailFacade>(resourceName);
    factory.registerFacade<Sink::ApplicationDomain::Folder, DummyResourceFolderFacade>(resourceName);
}

void DummyResourceFactory::registerAdaptorFactories(const QByteArray &resourceName, Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<Sink::ApplicationDomain::Event, DummyEventAdaptorFactory>(resourceName);
    registry.registerFactory<Sink::ApplicationDomain::Mail, DummyMailAdaptorFactory>(resourceName);
    registry.registerFactory<Sink::ApplicationDomain::Folder, DummyFolderAdaptorFactory>(resourceName);
}

void DummyResourceFactory::removeDataFromDisk(const QByteArray &instanceIdentifier)
{
    DummyResource::removeFromDisk(instanceIdentifier);
}
