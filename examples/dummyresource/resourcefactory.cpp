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
#include <QDate>
#include <QUuid>

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_EVENT "event"
#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

DummyResource::DummyResource(const QByteArray &instanceIdentifier, const QSharedPointer<Akonadi2::Pipeline> &pipeline)
    : Akonadi2::GenericResource(instanceIdentifier, pipeline),
    mEventAdaptorFactory(QSharedPointer<DummyEventAdaptorFactory>::create()),
    mMailAdaptorFactory(QSharedPointer<DummyMailAdaptorFactory>::create()),
    mFolderAdaptorFactory(QSharedPointer<DummyFolderAdaptorFactory>::create())
{
    addType(ENTITY_TYPE_MAIL, mMailAdaptorFactory,
            QVector<Akonadi2::Preprocessor*>() << new DefaultIndexUpdater<Akonadi2::ApplicationDomain::Mail>);
    addType(ENTITY_TYPE_FOLDER, mFolderAdaptorFactory,
            QVector<Akonadi2::Preprocessor*>() << new DefaultIndexUpdater<Akonadi2::ApplicationDomain::Folder>);
    addType(ENTITY_TYPE_EVENT, mEventAdaptorFactory,
            QVector<Akonadi2::Preprocessor*>() << new DefaultIndexUpdater<Akonadi2::ApplicationDomain::Event>);
}

Akonadi2::ApplicationDomain::Event::Ptr DummyResource::createEvent(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, Akonadi2::Storage::Transaction &transaction)
{
    static uint8_t rawData[100];
    auto event = Akonadi2::ApplicationDomain::Event::Ptr::create();
    event->setProperty("summary", data.value("summary").toString());
    event->setProperty("remoteId", ridBuffer);
    event->setProperty("description", data.value("description").toString());
    event->setProperty("attachment", QByteArray::fromRawData(reinterpret_cast<const char*>(rawData), 100));
    return event;
}

Akonadi2::ApplicationDomain::Mail::Ptr DummyResource::createMail(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, Akonadi2::Storage::Transaction &transaction)
{
    auto mail = Akonadi2::ApplicationDomain::Mail::Ptr::create();
    mail->setProperty("subject", data.value("subject").toString());
    mail->setProperty("senderEmail", data.value("senderEmail").toString());
    mail->setProperty("senderName", data.value("senderName").toString());
    mail->setProperty("date", data.value("date").toString());
    mail->setProperty("folder", resolveRemoteId(ENTITY_TYPE_FOLDER, data.value("parentFolder").toByteArray(), transaction));
    mail->setProperty("unread", data.value("unread").toBool());
    mail->setProperty("important", data.value("important").toBool());
    return mail;
}

Akonadi2::ApplicationDomain::Folder::Ptr DummyResource::createFolder(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, Akonadi2::Storage::Transaction &transaction)
{
    auto folder = Akonadi2::ApplicationDomain::Folder::Ptr::create();
    folder->setProperty("name", data.value("name").toString());
    folder->setProperty("icon", data.value("icon").toString());
    if (!data.value("parent").toString().isEmpty()) {
        auto akonadiId = resolveRemoteId(ENTITY_TYPE_FOLDER, data.value("parent").toByteArray(), transaction);
        folder->setProperty("parent", akonadiId);
    }
    return folder;
}

void DummyResource::synchronize(const QByteArray &bufferType, const QMap<QString, QMap<QString, QVariant> > &data, Akonadi2::Storage::Transaction &transaction, Akonadi2::Storage::Transaction &synchronizationTransaction, DomainTypeAdaptorFactoryInterface &adaptorFactory, std::function<Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, Akonadi2::Storage::Transaction &)> createEntity)
{
    //TODO find items to remove
    for (auto it = data.constBegin(); it != data.constEnd(); it++) {
        const auto remoteId = it.key().toUtf8();
        auto entity = createEntity(remoteId, it.value(), synchronizationTransaction);
        createOrModify(transaction, synchronizationTransaction, adaptorFactory, bufferType, remoteId, *entity);
    }
}

KAsync::Job<void> DummyResource::synchronizeWithSource(Akonadi2::Storage &mainStore, Akonadi2::Storage &synchronizationStore)
{
    Log() << " Synchronizing";
    return KAsync::start<void>([this, &mainStore, &synchronizationStore]() {
        auto transaction = mainStore.createTransaction(Akonadi2::Storage::ReadOnly);
        auto synchronizationTransaction = synchronizationStore.createTransaction(Akonadi2::Storage::ReadWrite);
        synchronize(ENTITY_TYPE_EVENT, DummyStore::instance().events(), transaction, synchronizationTransaction, *mEventAdaptorFactory, [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, Akonadi2::Storage::Transaction &synchronizationTransaction) {
            return createEvent(ridBuffer, data, synchronizationTransaction);
        });
        synchronize(ENTITY_TYPE_MAIL, DummyStore::instance().mails(), transaction, synchronizationTransaction, *mMailAdaptorFactory, [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, Akonadi2::Storage::Transaction &synchronizationTransaction) {
            return createMail(ridBuffer, data, synchronizationTransaction);
        });
        synchronize(ENTITY_TYPE_FOLDER, DummyStore::instance().folders(), transaction, synchronizationTransaction, *mFolderAdaptorFactory, [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, Akonadi2::Storage::Transaction &synchronizationTransaction) {
            return createFolder(ridBuffer, data, synchronizationTransaction);
        });
        Log() << "Done Synchronizing";
    });
}

KAsync::Job<void> DummyResource::replay(Akonadi2::Storage &synchronizationStore, const QByteArray &type, const QByteArray &key, const QByteArray &value)
{
    Trace() << "Replaying " << key;
    return KAsync::null<void>();
}

void DummyResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    GenericResource::removeFromDisk(instanceIdentifier);
    Akonadi2::Storage(Akonadi2::storageLocation(), instanceIdentifier + ".synchronization", Akonadi2::Storage::ReadWrite).removeFromDisk();
}

KAsync::Job<void> DummyResource::inspect(int inspectionType, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
{

    Trace() << "Inspecting " << inspectionType << domainType << entityId << property << expectedValue;
    if (property == "testInspection") {
        Akonadi2::ResourceNotification n;
        n.type = Akonadi2::NotificationType_Inspection;
        if (expectedValue.toBool()) {
            //Success
            n.code = 0;
            emit notify(n);
        } else {
            //Failure
            n.code = 1;
            emit notify(n);
        }
    }
    return KAsync::null<void>();
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
    factory.registerFacade<Akonadi2::ApplicationDomain::Folder, DummyResourceFolderFacade>(PLUGIN_NAME);
}

