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

void DummyResource::createEvent(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &transaction)
{
    static uint8_t rawData[100];
    Akonadi2::ApplicationDomain::Event event;
    event.setProperty("summary", data.value("summary").toString());
    event.setProperty("remoteId", ridBuffer);
    event.setProperty("description", data.value("description").toString());
    event.setProperty("attachment", QByteArray::fromRawData(reinterpret_cast<const char*>(rawData), 100));
    mEventAdaptorFactory->createBuffer(event, entityFbb);
}

QString DummyResource::resolveRemoteId(const QByteArray &bufferType, const QString &remoteId, Akonadi2::Storage::Transaction &transaction)
{
    //Lookup local id for remote id, or insert a new pair otherwise
    auto remoteIdWithType = bufferType + remoteId.toUtf8();
    QByteArray akonadiId = Index("rid.mapping", transaction).lookup(remoteIdWithType);
    if (akonadiId.isEmpty()) {
        akonadiId = QUuid::createUuid().toString().toUtf8();
        Index("rid.mapping", transaction).add(remoteIdWithType, akonadiId);
    }
    return akonadiId;
}


void DummyResource::createMail(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &transaction)
{
    Akonadi2::ApplicationDomain::Mail mail;
    mail.setProperty("subject", data.value("subject").toString());
    mail.setProperty("senderEmail", data.value("senderEmail").toString());
    mail.setProperty("senderName", data.value("senderName").toString());
    mail.setProperty("date", data.value("date").toString());
    mail.setProperty("folder", resolveRemoteId(ENTITY_TYPE_FOLDER, data.value("parentFolder").toString(), transaction));
    mail.setProperty("unread", data.value("unread").toBool());
    mail.setProperty("important", data.value("important").toBool());
    mMailAdaptorFactory->createBuffer(mail, entityFbb);
}

void DummyResource::createFolder(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &transaction)
{
    Akonadi2::ApplicationDomain::Folder folder;
    folder.setProperty("name", data.value("name").toString());
    folder.setProperty("icon", data.value("icon").toString());
    if (!data.value("parent").toString().isEmpty()) {
        auto akonadiId = resolveRemoteId(ENTITY_TYPE_FOLDER, data.value("parent").toString(), transaction);
        folder.setProperty("parent", akonadiId);
    }
    mFolderAdaptorFactory->createBuffer(folder, entityFbb);
}

void DummyResource::synchronize(const QString &bufferType, const QMap<QString, QMap<QString, QVariant> > &data, Akonadi2::Storage::Transaction &transaction, std::function<void(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &)> createEntity)
{
    Akonadi2::Storage store(Akonadi2::storageLocation(), mResourceInstanceIdentifier + ".synchronization", Akonadi2::Storage::ReadWrite);
    auto synchronizationTransaction = store.createTransaction(Akonadi2::Storage::ReadWrite);
    Index ridMapping("rid.mapping", synchronizationTransaction);
    for (auto it = data.constBegin(); it != data.constEnd(); it++) {
        const auto remoteId = it.key().toUtf8();
        auto akonadiId = resolveRemoteId(bufferType.toUtf8(), remoteId, synchronizationTransaction);

        bool found = false;
        transaction.openDatabase(bufferType.toUtf8() + ".main").scan(akonadiId.toUtf8(), [&found](const QByteArray &, const QByteArray &) -> bool {
            found = true;
            return false;
        }, [this](const Akonadi2::Storage::Error &error) {
        }, true);

        if (!found) { //A new entity
            flatbuffers::FlatBufferBuilder entityFbb;
            createEntity(remoteId, it.value(), entityFbb, synchronizationTransaction);

            flatbuffers::FlatBufferBuilder fbb;
            //This is the resource type and not the domain type
            auto entityId = fbb.CreateString(akonadiId.toStdString());
            auto type = fbb.CreateString(bufferType.toStdString());
            auto delta = Akonadi2::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
            auto location = Akonadi2::Commands::CreateCreateEntity(fbb, entityId, type, delta);
            Akonadi2::Commands::FinishCreateEntityBuffer(fbb, location);

            Trace() << "Found a new entity: " << remoteId;
            enqueueCommand(mSynchronizerQueue, Akonadi2::Commands::CreateEntityCommand, QByteArray::fromRawData(reinterpret_cast<char const *>(fbb.GetBufferPointer()), fbb.GetSize()));
        } else { //modification
            Trace() << "Found a modified entity: " << remoteId;
            //TODO diff and create modification if necessary
        }
    }
    //TODO find items to remove
}

KAsync::Job<void> DummyResource::synchronizeWithSource()
{
    Log() << " Synchronizing";
    return KAsync::start<void>([this](KAsync::Future<void> &f) {
        auto transaction = Akonadi2::Storage(Akonadi2::storageLocation(), mResourceInstanceIdentifier, Akonadi2::Storage::ReadOnly).createTransaction(Akonadi2::Storage::ReadOnly);

        synchronize(ENTITY_TYPE_EVENT, DummyStore::instance().events(), transaction, [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &synchronizationTransaction) {
            createEvent(ridBuffer, data, entityFbb, synchronizationTransaction);
        });
        synchronize(ENTITY_TYPE_MAIL, DummyStore::instance().mails(), transaction, [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &synchronizationTransaction) {
            createMail(ridBuffer, data, entityFbb, synchronizationTransaction);
        });
        synchronize(ENTITY_TYPE_FOLDER, DummyStore::instance().folders(), transaction, [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &synchronizationTransaction) {
            createFolder(ridBuffer, data, entityFbb, synchronizationTransaction);
        });

        f.setFinished();
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

