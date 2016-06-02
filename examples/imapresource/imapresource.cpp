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

#include "imapresource.h"
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
#include "inspection.h"
#include "synchronizer.h"
#include "sourcewriteback.h"
#include "entitystore.h"
#include "remoteidmap.h"
#include <QDate>
#include <QUuid>
#include <QDir>
#include <QDirIterator>

#include "imapserverproxy.h"

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

#undef DEBUG_AREA
#define DEBUG_AREA "resource.imap"

using namespace Imap;
using namespace Sink;

class MailPropertyExtractor : public Sink::Preprocessor
{
public:
    MailPropertyExtractor() {}

    void updatedIndexedProperties(Sink::ApplicationDomain::BufferAdaptor &newEntity)
    {
        const auto mimeMessagePath = newEntity.getProperty("mimeMessage").toString();
        Trace() << "Updating indexed properties " << mimeMessagePath;
        QFile f(mimeMessagePath);
        if (!f.open(QIODevice::ReadOnly)) {
            Warning() << "Failed to open the file: " << mimeMessagePath;
            return;
        }
        auto mapped = f.map(0, qMin((qint64)8000, f.size()));
        if (!mapped) {
            Warning() << "Failed to map file";
            return;
        }

        KMime::Message *msg = new KMime::Message;
        msg->setHead(KMime::CRLFtoLF(QByteArray::fromRawData(reinterpret_cast<const char*>(mapped), f.size())));
        msg->parse();

        newEntity.setProperty("subject", msg->subject(true)->asUnicodeString());
        newEntity.setProperty("sender", msg->from(true)->asUnicodeString());
        newEntity.setProperty("senderName", msg->from(true)->asUnicodeString());
        newEntity.setProperty("date", msg->date(true)->dateTime());
    }

    void newEntity(const QByteArray &uid, qint64 revision, Sink::ApplicationDomain::BufferAdaptor &newEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        updatedIndexedProperties(newEntity);
    }

    void modifiedEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::ApplicationDomain::BufferAdaptor &newEntity,
        Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        updatedIndexedProperties(newEntity);
    }

    void deletedEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
    }

};

class ImapSynchronizer : public Sink::Synchronizer {
public:
    ImapSynchronizer(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier)
        : Sink::Synchronizer(resourceType, resourceInstanceIdentifier)
    {

    }

    QByteArray createFolder(const QString &folderPath, const QByteArray &icon)
    {
        auto remoteId = folderPath.toUtf8();
        auto bufferType = ENTITY_TYPE_FOLDER;
        Sink::ApplicationDomain::Folder folder;
        auto folderPathParts = folderPath.split('/');
        const auto name = folderPathParts.takeLast();
        folder.setProperty("name", name);
        folder.setProperty("icon", icon);

        if (!folderPathParts.isEmpty()) {
            folder.setProperty("parent", syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, folderPathParts.join('/').toUtf8()));
        }
        createOrModify(bufferType, remoteId, folder);
        return remoteId;
    }

    void synchronizeFolders(const QVector<Folder> &folderList)
    {
        const QByteArray bufferType = ENTITY_TYPE_FOLDER;
        Trace() << "Found folders " << folderList.size();

        scanForRemovals(bufferType,
            [this, &bufferType](const std::function<void(const QByteArray &)> &callback) {
                //TODO Instead of iterating over all entries in the database, which can also pick up the same item multiple times,
                //we should rather iterate over an index that contains every uid exactly once. The remoteId index would be such an index,
                //but we currently fail to iterate over all entries in an index it seems.
                // auto remoteIds = synchronizationTransaction.openDatabase("rid.mapping." + bufferType, std::function<void(const Sink::Storage::Error &)>(), true);
                auto mainDatabase = Sink::Storage::mainDatabase(transaction(), bufferType);
                mainDatabase.scan("", [&](const QByteArray &key, const QByteArray &) {
                    if (Sink::Storage::isInternalKey(key)) {
                        return true;
                    }
                    callback(key);
                    return true;
                });
            },
            [&folderList](const QByteArray &remoteId) -> bool {
                //folderList.contains(remoteId)
                for (const auto folderPath : folderList) {
                    if (folderPath.pathParts.join('/') == remoteId) {
                        return true;
                    }
                }
                return false;
            }
        );

        for (const auto folderPath : folderList) {
            createFolder(folderPath.pathParts.join('/'), "folder");
        }
    }

    static QByteArray remoteIdForMessage(const QString &path, qint64 uid)
    {
        return path.toUtf8() + "/" + QByteArray::number(uid);
    }

    static qint64 uidFromMessageRemoteId(const QByteArray &remoteId)
    {
        return remoteId.split('/').last().toLongLong();
    }

    void synchronizeMails(const QString &path, const QVector<Message> &messages)
    {
        auto time = QSharedPointer<QTime>::create();
        time->start();
        const QByteArray bufferType = ENTITY_TYPE_MAIL;

        Trace() << "Importing new mail." << path;

        const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, path.toUtf8());

        int count = 0;
        for (const auto &message : messages) {
            count++;
            const auto remoteId = path.toUtf8() + "/" + QByteArray::number(message.uid);

            Trace() << "Found a mail " << remoteId << message.msg->subject(true)->asUnicodeString() << message.flags;

            Sink::ApplicationDomain::Mail mail;
            mail.setFolder(folderLocalId);

            const auto directory = Sink::resourceStorageLocation(mResourceInstanceIdentifier) + "/" + path.toUtf8();
            QDir().mkpath(directory);
            auto filePath = directory + "/" + QByteArray::number(message.uid);
            QFile file(filePath);
            if (!file.open(QIODevice::WriteOnly)) {
                Warning() << "Failed to open file for writing: " << file.errorString();
            }
            const auto content = message.msg->encodedContent();
            file.write(content);
            //Close before calling createOrModify, to ensure the file is available
            file.close();

            mail.setMimeMessagePath(filePath);
            mail.setUnread(!message.flags.contains(Imap::Flags::Seen));
            mail.setImportant(message.flags.contains(Imap::Flags::Flagged));

            createOrModify(bufferType, remoteId, mail);
        }
        commitSync();
        const auto elapsed = time->elapsed();
        Log() << "Synchronized " << count << " mails in " << path << Sink::Log::TraceTime(elapsed) << " " << elapsed/qMax(count, 1) << " [ms/mail]";
    }

    void synchronizeRemovals(const QString &path, const QSet<qint64> &messages)
    {
        auto time = QSharedPointer<QTime>::create();
        time->start();
        const QByteArray bufferType = ENTITY_TYPE_MAIL;

        Trace() << "Finding removed mail.";

        const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, path.toUtf8());

        int count = 0;
        auto property = Sink::ApplicationDomain::Mail::Folder::name;
        scanForRemovals(bufferType,
            [&](const std::function<void(const QByteArray &)> &callback) {
                Index index(bufferType + ".index." + property, transaction());
                index.lookup(folderLocalId, [&](const QByteArray &sinkId) {
                    callback(sinkId);
                },
                [&](const Index::Error &error) {
                    Warning() << "Error in index: " <<  error.message << property;
                });
            },
            [messages, path, &count](const QByteArray &remoteId) -> bool {
                if (messages.contains(uidFromMessageRemoteId(remoteId))) {
                    return true;
                }
                count++;
                return false;
            }
        );

        const auto elapsed = time->elapsed();
        Log() << "Removed " << count << " mails in " << path << Sink::Log::TraceTime(elapsed) << " " << elapsed/qMax(count, 1) << " [ms/mail]";
    }

    KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE
    {
        Log() << " Synchronizing";
        return KAsync::start<void>([this](KAsync::Future<void> future) {
            ImapServerProxy imap(mServer, mPort);
            auto loginFuture = imap.login(mUser, mPassword).exec();
            loginFuture.waitForFinished();
            if (loginFuture.errorCode()) {
                Warning() << "Login failed.";
                future.setError(1, "Login failed");
                return;
            } else {
                Trace() << "Login was successful";
            }

            QVector<Folder> folderList;
            auto folderFuture = imap.fetchFolders([this, &imap, &folderList](const QVector<Folder> &folders) {
                synchronizeFolders(folders);
                commit();
                commitSync();
                folderList << folders;

            }).exec();
            folderFuture.waitForFinished();
            if (folderFuture.errorCode()) {
                Warning() << "Folder sync failed.";
                future.setError(1, "Folder list sync failed");
                return;
            } else {
                Trace() << "Folder sync was successful";
            }

            for (const auto &folder : folderList) {
                QSet<qint64> uids;
                auto messagesFuture = imap.fetchMessages(folder, [this, folder, &uids](const QVector<Message> &messages) {
                    Trace() << "Synchronizing mails" << folder.normalizedPath();
                    for (const auto &msg : messages) {
                        uids << msg.uid;
                    }
                    synchronizeMails(folder.normalizedPath(), messages);
                    commit();
                    commitSync();
                }).exec();
                messagesFuture.waitForFinished();
                if (messagesFuture.errorCode()) {
                    future.setError(1, "Folder sync failed: " + folder.normalizedPath());
                    return;
                }
                //Remove what there is to remove
                synchronizeRemovals(folder.normalizedPath(), uids);
                commit();
                commitSync();
                Trace() << "Folder synchronized: " << folder.normalizedPath();
            }

            Log() << "Done Synchronizing";
            future.setFinished();
        });
    }

public:
    QString mServer;
    int mPort;
    QString mUser;
    QString mPassword;
    QByteArray mResourceInstanceIdentifier;
};

class ImapWriteback : public Sink::SourceWriteBack
{
public:
    ImapWriteback(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier) : Sink::SourceWriteBack(resourceType, resourceInstanceIdentifier)
    {

    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Mail &mail, Sink::Operation operation, const QByteArray &oldRemoteId)
    {
        return KAsync::null<QByteArray>();
    }

    QString buildPath(const ApplicationDomain::Folder &folder)
    {
        //Don't use entityStore in here, it can produce wrong results we're replaying an older revision than the latest one
        QChar separator = '/';
        QString path;
        auto parent = folder.getParent();
        if (!parent.isEmpty()) {
            auto parentRemoteId = syncStore().resolveLocalId("folder", parent);
            return parentRemoteId + separator.toLatin1() + folder.getName().toUtf8();
        }
        return separator + folder.getName();
    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Folder &folder, Sink::Operation operation, const QByteArray &oldRemoteId)
    {
        auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort);
        auto login = imap->login(mUser, mPassword);
        if (operation == Sink::Operation_Creation) {
            auto folderPath = buildPath(folder);
            auto imapPath = "INBOX" + folderPath.replace('/', '.');
            Trace() << "Creating a new folder: " << imapPath;
            return login.then<void>(imap->create(imapPath))
                .then<QByteArray>([imapPath, imap]() {
                    Trace() << "Finished creating a new folder: " << imapPath;
                    return imapPath.toUtf8();
                });
        } else if (operation == Sink::Operation_Removal) {
            Trace() << "Removing a folder: " << oldRemoteId;
            return login.then<void>(imap->remove(oldRemoteId))
                .then<QByteArray>([oldRemoteId, imap]() {
                    Trace() << "Finished removing a folder: " << oldRemoteId;
                    return QByteArray();
                });
        } else if (operation == Sink::Operation_Modification) {
            auto newFolderPath = buildPath(folder);
            auto newImapPath = "INBOX" + newFolderPath.replace('/', '.');
            Trace() << "Renaming a folder: " << oldRemoteId << newImapPath;
            return login.then<void>(imap->rename(oldRemoteId, newImapPath))
                .then<QByteArray>([newImapPath, imap]() {
                    Trace() << "Finished renaming a folder: " << newImapPath;
                    return newImapPath.toUtf8();
                });
        }
        return KAsync::null<QByteArray>();
    }

public:
    QString mServer;
    int mPort;
    QString mUser;
    QString mPassword;
    QByteArray mResourceInstanceIdentifier;
};

ImapResource::ImapResource(const QByteArray &instanceIdentifier, const QSharedPointer<Sink::Pipeline> &pipeline)
    : Sink::GenericResource(PLUGIN_NAME, instanceIdentifier, pipeline)
{
    auto config = ResourceConfig::getConfiguration(instanceIdentifier);
    mServer = config.value("server").toString();
    mPort = config.value("port").toInt();
    mUser = config.value("user").toString();
    mPassword = config.value("password").toString();

    auto synchronizer = QSharedPointer<ImapSynchronizer>::create(PLUGIN_NAME, instanceIdentifier);
    synchronizer->mServer = mServer;
    synchronizer->mPort = mPort;
    synchronizer->mUser = mUser;
    synchronizer->mPassword = mPassword;
    synchronizer->mResourceInstanceIdentifier = instanceIdentifier;
    setupSynchronizer(synchronizer);
    auto changereplay = QSharedPointer<ImapWriteback>::create(PLUGIN_NAME, instanceIdentifier);
    changereplay->mServer = mServer;
    changereplay->mPort = mPort;
    changereplay->mUser = mUser;
    changereplay->mPassword = mPassword;
    setupChangereplay(changereplay);

    setupPreprocessors(ENTITY_TYPE_MAIL, QVector<Sink::Preprocessor*>() << new MailPropertyExtractor << new DefaultIndexUpdater<Sink::ApplicationDomain::Mail>);
    setupPreprocessors(ENTITY_TYPE_FOLDER, QVector<Sink::Preprocessor*>() << new DefaultIndexUpdater<Sink::ApplicationDomain::Folder>);
}

void ImapResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    GenericResource::removeFromDisk(instanceIdentifier);
    Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".synchronization", Sink::Storage::ReadWrite).removeFromDisk();
}

KAsync::Job<void> ImapResource::inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
{
    auto synchronizationStore = QSharedPointer<Sink::Storage>::create(Sink::storageLocation(), mResourceInstanceIdentifier + ".synchronization", Sink::Storage::ReadOnly);
    auto synchronizationTransaction = synchronizationStore->createTransaction(Sink::Storage::ReadOnly);

    auto mainStore = QSharedPointer<Sink::Storage>::create(Sink::storageLocation(), mResourceInstanceIdentifier, Sink::Storage::ReadOnly);
    auto transaction = mainStore->createTransaction(Sink::Storage::ReadOnly);

    auto entityStore = QSharedPointer<Sink::EntityStore>::create(mResourceType, mResourceInstanceIdentifier, transaction);
    auto syncStore = QSharedPointer<Sink::RemoteIdMap>::create(synchronizationTransaction);

    Trace() << "Inspecting " << inspectionType << domainType << entityId << property << expectedValue;

    if (domainType == ENTITY_TYPE_MAIL) {
        const auto mail = entityStore->read<Sink::ApplicationDomain::Mail>(entityId);
        const auto folder = entityStore->read<Sink::ApplicationDomain::Folder>(mail.getFolder());
        const auto folderRemoteId = syncStore->resolveLocalId(ENTITY_TYPE_FOLDER, mail.getFolder());
        const auto mailRemoteId = syncStore->resolveLocalId(ENTITY_TYPE_MAIL, mail.identifier());
        if (mailRemoteId.isEmpty() || folderRemoteId.isEmpty()) {
            KAsync::error<void>();
        }
        Trace() << "Mail remote id: " << folderRemoteId << mailRemoteId << mail.identifier() << folder.identifier();

        if (inspectionType == Sink::ResourceControl::Inspection::PropertyInspectionType) {
            if (property == "unread") {
                return KAsync::null<void>();
            }
            if (property == "subject") {
                return KAsync::null<void>();
            }
        }
        if (inspectionType == Sink::ResourceControl::Inspection::ExistenceInspectionType) {
        }
    }
    if (domainType == ENTITY_TYPE_FOLDER) {
        const auto remoteId = syncStore->resolveLocalId(ENTITY_TYPE_FOLDER, entityId);
        const auto folder = entityStore->read<Sink::ApplicationDomain::Folder>(entityId);

        if (inspectionType == Sink::ResourceControl::Inspection::CacheIntegrityInspectionType) {
        }
        if (inspectionType == Sink::ResourceControl::Inspection::ExistenceInspectionType) {
            auto  folderByPath = QSharedPointer<QSet<QString>>::create();
            auto  folderByName = QSharedPointer<QSet<QString>>::create();

            auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort);
            auto inspectionJob = imap->login(mUser, mPassword)
                .then<void>(imap->fetchFolders([=](const QVector<Imap::Folder> &folders) {
                    for (const auto &f : folders) {
                        *folderByPath << f.normalizedPath();
                        *folderByName << f.pathParts.last();
                    }
                }))
                .then<void, KAsync::Job<void>>([folderByName, folderByPath, folder, remoteId, imap]() {
                    if (!folderByName->contains(folder.getName())) {
                        Warning() << "Existing folders are: " << *folderByPath;
                        Warning() << "We're looking for: " << folder.getName();
                        return KAsync::error<void>(1, "Wrong folder name: " + remoteId);
                    }
                    return KAsync::null<void>();
                });

            return inspectionJob;
        }

    }
    return KAsync::null<void>();
}

ImapResourceFactory::ImapResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent)
{

}

Sink::Resource *ImapResourceFactory::createResource(const QByteArray &instanceIdentifier)
{
    return new ImapResource(instanceIdentifier);
}

void ImapResourceFactory::registerFacades(Sink::FacadeFactory &factory)
{
    factory.registerFacade<Sink::ApplicationDomain::Mail, ImapResourceMailFacade>(PLUGIN_NAME);
    factory.registerFacade<Sink::ApplicationDomain::Folder, ImapResourceFolderFacade>(PLUGIN_NAME);
}

void ImapResourceFactory::registerAdaptorFactories(Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<Sink::ApplicationDomain::Mail, ImapMailAdaptorFactory>(PLUGIN_NAME);
    registry.registerFactory<Sink::ApplicationDomain::Folder, ImapFolderAdaptorFactory>(PLUGIN_NAME);
}
