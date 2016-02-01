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
#include "modifyentity_generated.h"
#include "deleteentity_generated.h"
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
#include "inspection.h"
#include <QDate>
#include <QUuid>
#include <QDir>
#include <QDirIterator>
#include <KMime/KMime/KMimeMessage>

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

MaildirResource::MaildirResource(const QByteArray &instanceIdentifier, const QSharedPointer<Sink::Pipeline> &pipeline)
    : Sink::GenericResource(instanceIdentifier, pipeline),
    mMailAdaptorFactory(QSharedPointer<MaildirMailAdaptorFactory>::create()),
    mFolderAdaptorFactory(QSharedPointer<MaildirFolderAdaptorFactory>::create())
{
    addType(ENTITY_TYPE_MAIL, mMailAdaptorFactory,
            QVector<Sink::Preprocessor*>() << new DefaultIndexUpdater<Sink::ApplicationDomain::Mail>);
    addType(ENTITY_TYPE_FOLDER, mFolderAdaptorFactory,
            QVector<Sink::Preprocessor*>() << new DefaultIndexUpdater<Sink::ApplicationDomain::Folder>);
    auto config = ResourceConfig::getConfiguration(instanceIdentifier);
    mMaildirPath = QDir::cleanPath(QDir::fromNativeSeparators(config.value("path").toString()));
    //Chop a trailing slash if necessary
    if (mMaildirPath.endsWith("/")) {
        mMaildirPath.chop(1);
    }
    Trace() << "Started maildir resource for maildir: " << mMaildirPath;
}

static QStringList listRecursive( const QString &root, const KPIM::Maildir &dir )
{
    QStringList list;
    foreach (const QString &sub, dir.subFolderList()) {
        const KPIM::Maildir md = dir.subFolder(sub);
        if (!md.isValid()) {
            continue;
        }
        QString path = root + "/" + sub;
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

void MaildirResource::synchronizeFolders(Sink::Storage::Transaction &transaction, Sink::Storage::Transaction &synchronizationTransaction)
{
    const QByteArray bufferType = ENTITY_TYPE_FOLDER;
    QStringList folderList = listAvailableFolders();
    Trace() << "Found folders " << folderList;

    scanForRemovals(transaction, synchronizationTransaction, bufferType,
        [&bufferType, &transaction](const std::function<void(const QByteArray &)> &callback) {
            //TODO Instead of iterating over all entries in the database, which can also pick up the same item multiple times,
            //we should rather iterate over an index that contains every uid exactly once. The remoteId index would be such an index,
            //but we currently fail to iterate over all entries in an index it seems.
            // auto remoteIds = synchronizationTransaction.openDatabase("rid.mapping." + bufferType, std::function<void(const Sink::Storage::Error &)>(), true);
            auto mainDatabase = Sink::Storage::mainDatabase(transaction, bufferType);
            mainDatabase.scan("", [&](const QByteArray &key, const QByteArray &) {
                callback(key);
                return true;
            });
        },
        [&folderList](const QByteArray &remoteId) -> bool {
            return folderList.contains(remoteId);
        }
    );

    for (const auto folderPath : folderList) {
        const auto remoteId = folderPath.toUtf8();
        Trace() << "Processing folder " << remoteId;
        KPIM::Maildir md(folderPath, folderPath == mMaildirPath);

        Sink::ApplicationDomain::Folder folder;
        folder.setProperty("name", md.name());
        folder.setProperty("icon", "folder");
        if (!md.isRoot()) {
            folder.setProperty("parent", resolveRemoteId(ENTITY_TYPE_FOLDER, md.parent().path().toUtf8(), synchronizationTransaction));
        }
        createOrModify(transaction, synchronizationTransaction, *mFolderAdaptorFactory, bufferType, remoteId, folder);
    }
}

void MaildirResource::synchronizeMails(Sink::Storage::Transaction &transaction, Sink::Storage::Transaction &synchronizationTransaction, const QString &path)
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

    const auto folderLocalId = resolveRemoteId(ENTITY_TYPE_FOLDER, path.toUtf8(), synchronizationTransaction);

    auto property = "folder";
    scanForRemovals(transaction, synchronizationTransaction, bufferType,
        [&](const std::function<void(const QByteArray &)> &callback) {
            Index index(bufferType + ".index." + property, transaction);
            index.lookup(folderLocalId, [&](const QByteArray &sinkId) {
                callback(sinkId);
            },
            [&](const Index::Error &error) {
                Warning() << "Error in index: " <<  error.message << property;
            });
        },
        [](const QByteArray &remoteId) -> bool {
            return QFile(remoteId).exists();
        }
    );

    while (entryIterator->hasNext()) {
        const QString filePath = QDir::fromNativeSeparators(entryIterator->next());
        const QString fileName = entryIterator->fileName();
        const auto remoteId = filePath.toUtf8();

        KMime::Message *msg = new KMime::Message;
        msg->setHead(KMime::CRLFtoLF(maildir.readEntryHeadersFromFile(filePath)));
        msg->parse();

        const auto flags = maildir.readEntryFlags(fileName);

        Trace() << "Found a mail " << filePath << " : " << fileName << msg->subject(true)->asUnicodeString();

        Sink::ApplicationDomain::Mail mail;
        mail.setProperty("subject", msg->subject(true)->asUnicodeString());
        mail.setProperty("sender", msg->from(true)->asUnicodeString());
        mail.setProperty("senderName", msg->from(true)->asUnicodeString());
        mail.setProperty("date", msg->date(true)->dateTime());
        mail.setProperty("folder", folderLocalId);
        mail.setProperty("mimeMessage", filePath);
        mail.setProperty("unread", !flags.testFlag(KPIM::Maildir::Seen));
        mail.setProperty("important", flags.testFlag(KPIM::Maildir::Flagged));

        createOrModify(transaction, synchronizationTransaction, *mMailAdaptorFactory, bufferType, remoteId, mail);
    }
}

KAsync::Job<void> MaildirResource::synchronizeWithSource(Sink::Storage &mainStore, Sink::Storage &synchronizationStore)
{
    Log() << " Synchronizing";
    return KAsync::start<void>([this, &mainStore, &synchronizationStore]() {
        auto transaction = mainStore.createTransaction(Sink::Storage::ReadOnly);
        {
            auto synchronizationTransaction = synchronizationStore.createTransaction(Sink::Storage::ReadWrite);
            synchronizeFolders(transaction, synchronizationTransaction);
            //The next sync needs the folders available
            synchronizationTransaction.commit();
        }
        for (const auto &folder : listAvailableFolders()) {
            auto synchronizationTransaction = synchronizationStore.createTransaction(Sink::Storage::ReadWrite);
            synchronizeMails(transaction, synchronizationTransaction, folder);
            //Don't let the transaction grow too much
            synchronizationTransaction.commit();
        }
        Log() << "Done Synchronizing";
    });
}

KAsync::Job<void> MaildirResource::replay(Sink::Storage &synchronizationStore, const QByteArray &type, const QByteArray &key, const QByteArray &value)
{
    auto synchronizationTransaction = synchronizationStore.createTransaction(Sink::Storage::ReadWrite);

    Trace() << "Replaying " << key << type;
    if (type == ENTITY_TYPE_FOLDER) {
        Sink::EntityBuffer buffer(value.data(), value.size());
        const Sink::Entity &entity = buffer.entity();
        const auto metadataBuffer = Sink::EntityBuffer::readBuffer<Sink::Metadata>(entity.metadata());
        if (metadataBuffer && !metadataBuffer->replayToSource()) {
            Trace() << "Change is coming from the source";
            return KAsync::null<void>();
        }
        const qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
        const auto operation = metadataBuffer ? metadataBuffer->operation() : Sink::Operation_Creation;
        if (operation == Sink::Operation_Creation) {
            const Sink::ApplicationDomain::Folder folder(mResourceInstanceIdentifier, Sink::Storage::uidFromKey(key), revision, mFolderAdaptorFactory->createAdaptor(entity));
            auto folderName = folder.getProperty("name").toString();
            //TODO handle non toplevel folders
            auto path = mMaildirPath + "/" + folderName;
            Trace() << "Creating a new folder: " << path;
            KPIM::Maildir maildir(path, false);
            maildir.create();
            recordRemoteId(ENTITY_TYPE_FOLDER, folder.identifier(), path.toUtf8(), synchronizationTransaction);
        } else if (operation == Sink::Operation_Removal) {
            const auto uid = Sink::Storage::uidFromKey(key);
            const auto remoteId = resolveLocalId(ENTITY_TYPE_FOLDER, uid, synchronizationTransaction);
            const auto path = remoteId;
            Trace() << "Removing a folder: " << path;
            KPIM::Maildir maildir(path, false);
            maildir.remove();
            removeRemoteId(ENTITY_TYPE_FOLDER, uid, remoteId, synchronizationTransaction);
        } else if (operation == Sink::Operation_Modification) {
            Warning() << "Folder modifications are not implemented";
        } else {
            Warning() << "Unkown operation" << operation;
        }
    } else if (type == ENTITY_TYPE_MAIL) {
        Sink::EntityBuffer buffer(value.data(), value.size());
        const Sink::Entity &entity = buffer.entity();
        const auto metadataBuffer = Sink::EntityBuffer::readBuffer<Sink::Metadata>(entity.metadata());
        if (metadataBuffer && !metadataBuffer->replayToSource()) {
            Trace() << "Change is coming from the source";
            return KAsync::null<void>();
        }
        const qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
        const auto operation = metadataBuffer ? metadataBuffer->operation() : Sink::Operation_Creation;
        if (operation == Sink::Operation_Creation) {
            const Sink::ApplicationDomain::Mail mail(mResourceInstanceIdentifier, Sink::Storage::uidFromKey(key), revision, mMailAdaptorFactory->createAdaptor(entity));
            auto parentFolder = mail.getProperty("folder").toByteArray();
            QByteArray parentFolderRemoteId;
            if (!parentFolder.isEmpty()) {
                parentFolderRemoteId = resolveLocalId(ENTITY_TYPE_FOLDER, parentFolder, synchronizationTransaction);
            } else {
                parentFolderRemoteId = mMaildirPath.toUtf8();
            }
            const auto parentFolderPath = parentFolderRemoteId;
            KPIM::Maildir maildir(parentFolderPath, false);
            //FIXME assemble the MIME message
            const auto id = maildir.addEntry("foobar");
            Trace() << "Creating a new mail: " << id;
            recordRemoteId(ENTITY_TYPE_MAIL, mail.identifier(), id.toUtf8(), synchronizationTransaction);
        } else if (operation == Sink::Operation_Removal) {
            const auto uid = Sink::Storage::uidFromKey(key);
            const auto remoteId = resolveLocalId(ENTITY_TYPE_MAIL, uid, synchronizationTransaction);
            Trace() << "Removing a mail: " << remoteId;
            QFile::remove(remoteId);
            removeRemoteId(ENTITY_TYPE_MAIL, uid, remoteId, synchronizationTransaction);
        } else if (operation == Sink::Operation_Modification) {
            Warning() << "Mail modifications are not implemented";
        } else {
            Warning() << "Unkown operation" << operation;
        }
    }
    return KAsync::null<void>();
}

void MaildirResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    GenericResource::removeFromDisk(instanceIdentifier);
    Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".synchronization", Sink::Storage::ReadWrite).removeFromDisk();
}

KAsync::Job<void> MaildirResource::inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
{
    auto synchronizationStore = QSharedPointer<Sink::Storage>::create(Sink::storageLocation(), mResourceInstanceIdentifier + ".synchronization", Sink::Storage::ReadOnly);
    auto synchronizationTransaction = synchronizationStore->createTransaction(Sink::Storage::ReadOnly);
    Trace() << "Inspecting " << inspectionType << domainType << entityId << property << expectedValue;
    if (domainType == ENTITY_TYPE_MAIL) {
        if (inspectionType == Sink::Resources::Inspection::PropertyInspectionType) {
            if (property == "unread") {
                const auto remoteId = resolveLocalId(ENTITY_TYPE_MAIL, entityId, synchronizationTransaction);
                const auto flags = KPIM::Maildir::readEntryFlags(remoteId.split('/').last());
                if (expectedValue.toBool() && !(flags & KPIM::Maildir::Seen)) {
                    return KAsync::error<void>(1, "Expected seen but couldn't find it.");
                }
                if (!expectedValue.toBool() && (flags & KPIM::Maildir::Seen)) {
                    return KAsync::error<void>(1, "Expected seen but couldn't find it.");
                }
                return KAsync::null<void>();
            }
            if (property == "subject") {
                const auto remoteId = resolveLocalId(ENTITY_TYPE_MAIL, entityId, synchronizationTransaction);

                KMime::Message *msg = new KMime::Message;
                msg->setHead(KMime::CRLFtoLF(KPIM::Maildir::readEntryHeadersFromFile(remoteId)));
                msg->parse();

                if (msg->subject(true)->asUnicodeString() != expectedValue.toString()) {
                    return KAsync::error<void>(1, "Subject not as expected.");
                }
                return KAsync::null<void>();
            }
        }
        if (inspectionType == Sink::Resources::Inspection::ExistenceInspectionType) {
            const auto remoteId = resolveLocalId(ENTITY_TYPE_MAIL, entityId, synchronizationTransaction);
            if (QFileInfo(remoteId).exists() != expectedValue.toBool()) {
                return KAsync::error<void>(1, "Wrong file existence: " + remoteId);
            }
        }
    }
    return KAsync::null<void>();
}

MaildirResourceFactory::MaildirResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent)
{

}

Sink::Resource *MaildirResourceFactory::createResource(const QByteArray &instanceIdentifier)
{
    return new MaildirResource(instanceIdentifier);
}

void MaildirResourceFactory::registerFacades(Sink::FacadeFactory &factory)
{
    factory.registerFacade<Sink::ApplicationDomain::Mail, MaildirResourceMailFacade>(PLUGIN_NAME);
    factory.registerFacade<Sink::ApplicationDomain::Folder, MaildirResourceFolderFacade>(PLUGIN_NAME);
}

