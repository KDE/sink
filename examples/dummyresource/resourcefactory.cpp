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
#include "domainadaptor.h"
#include "log.h"
#include "dummystore.h"
#include "definitions.h"
#include "facadefactory.h"
#include "adaptorfactoryregistry.h"
#include "synchronizer.h"
#include "inspector.h"
#include "mailpreprocessor.h"
#include "specialpurposepreprocessor.h"
#include <QDate>

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_EVENT "event"
#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

using namespace Sink;

class DummySynchronizer : public Sink::Synchronizer {
    public:

    DummySynchronizer(const Sink::ResourceContext &context)
        : Sink::Synchronizer(context)
    {
        setSecret("dummy");
    }

    Sink::ApplicationDomain::Event::Ptr createEvent(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data)
    {
        auto event = Sink::ApplicationDomain::Event::Ptr::create();
        event->setSummary(data.value("summary").toString());
        event->setProperty("remoteId", ridBuffer);
        event->setDescription(data.value("description").toString());
        return event;
    }

    Sink::ApplicationDomain::Mail::Ptr createMail(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data)
    {
        auto mail = Sink::ApplicationDomain::Mail::Ptr::create();
        mail->setExtractedMessageId(ridBuffer);
        mail->setExtractedSubject(data.value("subject").toString());
        mail->setExtractedSender(Sink::ApplicationDomain::Mail::Contact{data.value("senderName").toString(), data.value("senderEmail").toString()});
        mail->setExtractedDate(data.value("date").toDateTime());
        mail->setFolder(syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, data.value("parentFolder").toByteArray()));
        mail->setUnread(data.value("unread").toBool());
        mail->setImportant(data.value("important").toBool());
        return mail;
    }

    Sink::ApplicationDomain::Folder::Ptr createFolder(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data)
    {
        auto folder = Sink::ApplicationDomain::Folder::Ptr::create();
        folder->setName(data.value("name").toString());
        folder->setIcon(data.value("icon").toByteArray());
        if (!data.value("parent").toString().isEmpty()) {
            auto sinkId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, data.value("parent").toByteArray());
            folder->setParent(sinkId);
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
        SinkTrace() << "Synchronize with source and sending a notification about it";
        Sink::Notification n;
        n.id = "connected";
        n.type = Sink::Notification::Status;
        n.message = "We're connected";
        n.code = Sink::ApplicationDomain::ConnectedStatus;
        emit notify(n);
        return KAsync::start([this]() {
            synchronize(ENTITY_TYPE_EVENT, DummyStore::instance().events(), [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data) {
                return createEvent(ridBuffer, data);
            });
            synchronize(ENTITY_TYPE_MAIL, DummyStore::instance().mails(), [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data) {
                return createMail(ridBuffer, data);
            });
            synchronize(ENTITY_TYPE_FOLDER, DummyStore::instance().folders(), [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data) {
                return createFolder(ridBuffer, data);
            });
        });
    }

    bool canReplay(const QByteArray &type, const QByteArray &key, const QByteArray &value) Q_DECL_OVERRIDE { return false; }

};

class DummyInspector : public Sink::Inspector {
public:
    DummyInspector(const Sink::ResourceContext &resourceContext)
        : Sink::Inspector(resourceContext)
    {

    }

protected:
    KAsync::Job<void> inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue) Q_DECL_OVERRIDE
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
};

DummyResource::DummyResource(const Sink::ResourceContext &resourceContext, const QSharedPointer<Sink::Pipeline> &pipeline)
    : Sink::GenericResource(resourceContext, pipeline)
{
    setupSynchronizer(QSharedPointer<DummySynchronizer>::create(resourceContext));
    setupInspector(QSharedPointer<DummyInspector>::create(resourceContext));
    setupPreprocessors(ENTITY_TYPE_MAIL,
            QVector<Sink::Preprocessor*>() << new MailPropertyExtractor << new SpecialPurposeProcessor);
    setupPreprocessors(ENTITY_TYPE_FOLDER,
            QVector<Sink::Preprocessor*>());
    setupPreprocessors(ENTITY_TYPE_EVENT,
            QVector<Sink::Preprocessor*>());
}

DummyResource::~DummyResource()
{

}

DummyResourceFactory::DummyResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent, {Sink::ApplicationDomain::ResourceCapabilities::Mail::mail,
            "event",
            Sink::ApplicationDomain::ResourceCapabilities::Mail::folder,
            Sink::ApplicationDomain::ResourceCapabilities::Mail::storage,
            "-folder.rename",
            Sink::ApplicationDomain::ResourceCapabilities::Mail::sent}
            )
{

}

Sink::Resource *DummyResourceFactory::createResource(const Sink::ResourceContext &resourceContext)
{
    return new DummyResource(resourceContext);
}

void DummyResourceFactory::registerFacades(const QByteArray &resourceName, Sink::FacadeFactory &factory)
{
    factory.registerFacade<ApplicationDomain::Event, DefaultFacade<ApplicationDomain::Event>>(resourceName);
    factory.registerFacade<ApplicationDomain::Mail, DefaultFacade<ApplicationDomain::Mail>>(resourceName);
    factory.registerFacade<ApplicationDomain::Folder, DefaultFacade<ApplicationDomain::Folder>>(resourceName);
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

