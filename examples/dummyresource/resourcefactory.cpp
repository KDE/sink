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
#include "indexupdater.h"
#include "adaptorfactoryregistry.h"
#include "synchronizer.h"
#include "remoteidmap.h"
#include <QDate>
#include <QUuid>

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_EVENT "event"
#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

class DummySynchronizer : public Sink::Synchronizer {
    public:

    DummySynchronizer(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier)
        : Sink::Synchronizer(resourceType, resourceInstanceIdentifier)
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
        Trace() << "Sync of " << count << " entities of type " << bufferType << " done." << Sink::Log::TraceTime(time->elapsed());
    }

    KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE
    {
        Log() << " Synchronizing with the source";
        return KAsync::start<void>([this]() {
            synchronize(ENTITY_TYPE_EVENT, DummyStore::instance().events(), [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data) {
                return createEvent(ridBuffer, data);
            });
            synchronize(ENTITY_TYPE_MAIL, DummyStore::instance().mails(), [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data) {
                return createMail(ridBuffer, data);
            });
            synchronize(ENTITY_TYPE_FOLDER, DummyStore::instance().folders(), [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data) {
                return createFolder(ridBuffer, data);
            });
            Log() << "Done Synchronizing";
        });
    }

};

DummyResource::DummyResource(const QByteArray &instanceIdentifier, const QSharedPointer<Sink::Pipeline> &pipeline)
    : Sink::GenericResource(PLUGIN_NAME, instanceIdentifier, pipeline)
{
    setupSynchronizer(QSharedPointer<DummySynchronizer>::create(PLUGIN_NAME, instanceIdentifier));
    setupChangereplay(QSharedPointer<Sink::NullChangeReplay>::create());
    setupPreprocessors(ENTITY_TYPE_MAIL,
            QVector<Sink::Preprocessor*>() << new DefaultIndexUpdater<Sink::ApplicationDomain::Mail>);
    setupPreprocessors(ENTITY_TYPE_FOLDER,
            QVector<Sink::Preprocessor*>() << new DefaultIndexUpdater<Sink::ApplicationDomain::Folder>);
    setupPreprocessors(ENTITY_TYPE_EVENT,
            QVector<Sink::Preprocessor*>() << new DefaultIndexUpdater<Sink::ApplicationDomain::Event>);
}

DummyResource::~DummyResource()
{

}

void DummyResource::removeDataFromDisk()
{
    removeFromDisk(mResourceInstanceIdentifier);
}

void DummyResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    GenericResource::removeFromDisk(instanceIdentifier);
    Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".synchronization", Sink::Storage::ReadWrite).removeFromDisk();
}

KAsync::Job<void> DummyResource::inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
{

    Trace() << "Inspecting " << inspectionType << domainType << entityId << property << expectedValue;
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

Sink::Resource *DummyResourceFactory::createResource(const QByteArray &instanceIdentifier)
{
    return new DummyResource(instanceIdentifier);
}

void DummyResourceFactory::registerFacades(Sink::FacadeFactory &factory)
{
    factory.registerFacade<Sink::ApplicationDomain::Event, DummyResourceFacade>(PLUGIN_NAME);
    factory.registerFacade<Sink::ApplicationDomain::Mail, DummyResourceMailFacade>(PLUGIN_NAME);
    factory.registerFacade<Sink::ApplicationDomain::Folder, DummyResourceFolderFacade>(PLUGIN_NAME);
}

void DummyResourceFactory::registerAdaptorFactories(Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<Sink::ApplicationDomain::Folder, DummyFolderAdaptorFactory>(PLUGIN_NAME);
    registry.registerFactory<Sink::ApplicationDomain::Mail, DummyMailAdaptorFactory>(PLUGIN_NAME);
    registry.registerFactory<Sink::ApplicationDomain::Event, DummyEventAdaptorFactory>(PLUGIN_NAME);
}

