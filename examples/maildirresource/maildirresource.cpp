/*
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

#include "maildirresource.h"
#include "facade.h"
#include "entitybuffer.h"
#include "pipeline.h"
#include "mail_generated.h"
#include "createentity_generated.h"
#include "domainadaptor.h"
#include "resourceconfig.h"
#include "commands.h"
#include "index.h"
#include "log.h"
#include "domain/mail.h"
#include "definitions.h"
#include "facadefactory.h"
#include "indexupdater.h"
#include "libmaildir/maildir.h"
#include <QDate>
#include <QUuid>
#include <QDir>
#include <QDirIterator>
#include <KMime/KMime/KMimeMessage>

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

MaildirResource::MaildirResource(const QByteArray &instanceIdentifier, const QSharedPointer<Akonadi2::Pipeline> &pipeline)
    : Akonadi2::GenericResource(instanceIdentifier, pipeline),
    mMailAdaptorFactory(QSharedPointer<MaildirMailAdaptorFactory>::create()),
    mFolderAdaptorFactory(QSharedPointer<MaildirFolderAdaptorFactory>::create())
{
    addType(ENTITY_TYPE_MAIL, mMailAdaptorFactory,
            QVector<Akonadi2::Preprocessor*>() << new DefaultIndexUpdater<Akonadi2::ApplicationDomain::Mail>);
    addType(ENTITY_TYPE_FOLDER, mFolderAdaptorFactory,
            QVector<Akonadi2::Preprocessor*>() << new DefaultIndexUpdater<Akonadi2::ApplicationDomain::Folder>);
    auto config = ResourceConfig::getConfiguration(instanceIdentifier);
    mMaildirPath = config.value("path").toString();
    //Chop a trailing slash if necessary
    if (mMaildirPath.endsWith(QDir::separator())) {
        mMaildirPath.chop(1);
    }
    Trace() << "Started maildir resource for maildir: " << mMaildirPath;
}

QString MaildirResource::resolveRemoteId(const QByteArray &bufferType, const QString &remoteId, Akonadi2::Storage::Transaction &transaction)
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

static QStringList listRecursive( const QString &root, const KPIM::Maildir &dir )
{
    QStringList list;
    foreach (const QString &sub, dir.subFolderList()) {
        const KPIM::Maildir md = dir.subFolder(sub);
        if (!md.isValid()) {
            continue;
        }
        QString path = root + QDir::separator() + sub;
        list << path;
        list += listRecursive(path, md );
    }
    return list;
}

QStringList MaildirResource::listAvailableFolders()
{
    KPIM::Maildir dir(mMaildirPath, true);
    if (!dir.isValid()) {
        return QStringList();
    }
    QStringList folderList;
    folderList << mMaildirPath;
    folderList += listRecursive(mMaildirPath, dir);
    return folderList;
}

static void createEntity(const QByteArray &akonadiId, const QByteArray &bufferType, Akonadi2::ApplicationDomain::ApplicationDomainType &domainObject, DomainTypeAdaptorFactoryInterface &adaptorFactory, std::function<void(const QByteArray &)> callback)
{
    flatbuffers::FlatBufferBuilder entityFbb;
    adaptorFactory.createBuffer(domainObject, entityFbb);
    flatbuffers::FlatBufferBuilder fbb;
    //This is the resource type and not the domain type
    auto entityId = fbb.CreateString(akonadiId.toStdString());
    auto type = fbb.CreateString(bufferType.toStdString());
    auto delta = Akonadi2::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
    auto location = Akonadi2::Commands::CreateCreateEntity(fbb, entityId, type, delta);
    Akonadi2::Commands::FinishCreateEntityBuffer(fbb, location);
    callback(QByteArray::fromRawData(reinterpret_cast<char const *>(fbb.GetBufferPointer()), fbb.GetSize()));
}

void MaildirResource::synchronizeFolders(Akonadi2::Storage::Transaction &transaction)
{
    const QByteArray bufferType = ENTITY_TYPE_FOLDER;
    QStringList folderList = listAvailableFolders();
    Trace() << "Found folders " << folderList;

    Akonadi2::Storage store(Akonadi2::storageLocation(), mResourceInstanceIdentifier + ".synchronization", Akonadi2::Storage::ReadWrite);
    auto synchronizationTransaction = store.createTransaction(Akonadi2::Storage::ReadWrite);
    for (const auto folder : folderList) {
        const auto remoteId = folder.toUtf8();
        Trace() << "Processing folder " << remoteId;
        auto akonadiId = resolveRemoteId(bufferType, remoteId, synchronizationTransaction);

        bool found = false;
        transaction.openDatabase(bufferType + ".main").scan(akonadiId.toUtf8(), [&found](const QByteArray &, const QByteArray &) -> bool {
            found = true;
            return false;
        }, [this](const Akonadi2::Storage::Error &error) {
        }, true);

        if (!found) { //A new entity
            KPIM::Maildir md(folder, folder == mMaildirPath);

            Akonadi2::ApplicationDomain::Folder folder;
            folder.setProperty("name", md.name());
            folder.setProperty("icon", "folder");
            if (!md.isRoot()) {
                Trace() << "subfolder parent: " << md.parent().path();
                auto akonadiId = resolveRemoteId(ENTITY_TYPE_FOLDER, md.parent().path(), synchronizationTransaction);
                folder.setProperty("parent", akonadiId);
            }

            Trace() << "Found a new entity: " << remoteId;
            createEntity(akonadiId.toLatin1(), bufferType, folder, *mFolderAdaptorFactory, [this](const QByteArray &buffer) {
                enqueueCommand(mSynchronizerQueue, Akonadi2::Commands::CreateEntityCommand, buffer);
            });

        } else { //modification
            Trace() << "Found a modified entity: " << remoteId;
            //TODO diff and create modification if necessary
        }
    }
    //TODO find items to remove
}

void MaildirResource::synchronizeMails(Akonadi2::Storage::Transaction &transaction, const QString &path)
{
    Trace() << "Synchronizing mails" << path;
    const QByteArray bufferType = ENTITY_TYPE_MAIL;

    KPIM::Maildir maildir(path, true);
    if (!maildir.isValid()) {
        Warning() << "Failed to sync folder " << maildir.lastError();
        return;
    }

    auto listingPath = maildir.pathToCurrent();
    auto entryIterator = QSharedPointer<QDirIterator>::create(listingPath, QDir::Files);
    Trace() << "Looking into " << listingPath;

    QFileInfo entryInfo;

    Akonadi2::Storage store(Akonadi2::storageLocation(), mResourceInstanceIdentifier + ".synchronization", Akonadi2::Storage::ReadWrite);
    auto synchronizationTransaction = store.createTransaction(Akonadi2::Storage::ReadWrite);

    while (entryIterator->hasNext()) {
        QString filePath = entryIterator->next();
        QString fileName = entryIterator->fileName();

        const auto remoteId = fileName.toUtf8();
        auto akonadiId = resolveRemoteId(bufferType, remoteId, synchronizationTransaction);

        bool found = false;
        transaction.openDatabase(bufferType + ".main").scan(akonadiId.toUtf8(), [&found](const QByteArray &, const QByteArray &) -> bool {
            found = true;
            return false;
        }, [this](const Akonadi2::Storage::Error &error) {
        }, true);

        if (!found) { //A new entity
            KMime::Message *msg = new KMime::Message;
            auto filepath = listingPath + QDir::separator() + fileName;
            msg->setHead(KMime::CRLFtoLF(maildir.readEntryHeadersFromFile(filepath)));
            msg->parse();

            const auto flags = maildir.readEntryFlags(fileName);

            Trace() << "Found a mail " << filePath << fileName << msg->subject(true)->asUnicodeString();

            Akonadi2::ApplicationDomain::Mail mail;
            mail.setProperty("subject", msg->subject(true)->asUnicodeString());
            mail.setProperty("sender", msg->from(true)->asUnicodeString());
            mail.setProperty("senderName", msg->from(true)->asUnicodeString());
            mail.setProperty("date", msg->date(true)->dateTime().toString());
            mail.setProperty("folder", resolveRemoteId(ENTITY_TYPE_FOLDER, path, synchronizationTransaction));
            mail.setProperty("mimeMessage", filepath);
            mail.setProperty("unread", !flags.testFlag(KPIM::Maildir::Seen));
            mail.setProperty("important", flags.testFlag(KPIM::Maildir::Flagged));

            flatbuffers::FlatBufferBuilder entityFbb;
            mMailAdaptorFactory->createBuffer(mail, entityFbb);

            Trace() << "Found a new entity: " << remoteId;
            createEntity(akonadiId.toLatin1(), bufferType, mail, *mMailAdaptorFactory, [this](const QByteArray &buffer) {
                enqueueCommand(mSynchronizerQueue, Akonadi2::Commands::CreateEntityCommand, buffer);
            });
        } else { //modification
            Trace() << "Found a modified entity: " << remoteId;
            //TODO diff and create modification if necessary
        }
    }
    //TODO find items to remove
}

KAsync::Job<void> MaildirResource::synchronizeWithSource()
{
    Log() << " Synchronizing";
    return KAsync::start<void>([this]() {
        auto transaction = Akonadi2::Storage(Akonadi2::storageLocation(), mResourceInstanceIdentifier, Akonadi2::Storage::ReadOnly).createTransaction(Akonadi2::Storage::ReadOnly);
        synchronizeFolders(transaction);
        for (const auto &folder : listAvailableFolders()) {
            synchronizeMails(transaction, folder);
        }
    });
}

KAsync::Job<void> MaildirResource::replay(const QByteArray &type, const QByteArray &key, const QByteArray &value)
{
    Trace() << "Replaying " << key;
    return KAsync::null<void>();
}

void MaildirResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    GenericResource::removeFromDisk(instanceIdentifier);
    Akonadi2::Storage(Akonadi2::storageLocation(), instanceIdentifier + ".synchronization", Akonadi2::Storage::ReadWrite).removeFromDisk();
}

MaildirResourceFactory::MaildirResourceFactory(QObject *parent)
    : Akonadi2::ResourceFactory(parent)
{

}

Akonadi2::Resource *MaildirResourceFactory::createResource(const QByteArray &instanceIdentifier)
{
    return new MaildirResource(instanceIdentifier);
}

void MaildirResourceFactory::registerFacades(Akonadi2::FacadeFactory &factory)
{
    factory.registerFacade<Akonadi2::ApplicationDomain::Mail, MaildirResourceMailFacade>(PLUGIN_NAME);
    factory.registerFacade<Akonadi2::ApplicationDomain::Folder, MaildirResourceFolderFacade>(PLUGIN_NAME);
}

