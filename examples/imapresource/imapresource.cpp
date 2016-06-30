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
#include "query.h"
#include <QDate>
#include <QUuid>
#include <QDir>
#include <QDirIterator>

#include "imapserverproxy.h"
#include "entityreader.h"
#include "mailpreprocessor.h"
#include "specialpurposepreprocessor.h"

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

#undef DEBUG_AREA
#define DEBUG_AREA "resource.imap"

using namespace Imap;
using namespace Sink;

static qint64 uidFromMailRid(const QByteArray &remoteId)
{
    auto ridParts = remoteId.split(':');
    Q_ASSERT(ridParts.size() == 2);
    return ridParts.last().toLongLong();
}

static QByteArray folderIdFromMailRid(const QByteArray &remoteId)
{
    auto ridParts = remoteId.split(':');
    Q_ASSERT(ridParts.size() == 2);
    return ridParts.first();
}

static QByteArray assembleMailRid(const QByteArray &folderLocalId, qint64 imapUid)
{
    return folderLocalId + ':' + QByteArray::number(imapUid);
}

static QByteArray assembleMailRid(const ApplicationDomain::Mail &mail, qint64 imapUid)
{
    return assembleMailRid(mail.getFolder(), imapUid);
}


class ImapSynchronizer : public Sink::Synchronizer {
public:
    ImapSynchronizer(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier)
        : Sink::Synchronizer(resourceType, resourceInstanceIdentifier)
    {

    }

    QByteArray createFolder(const QString &folderName, const QString &folderPath, const QString &parentFolderRid, const QByteArray &icon)
    {
        Trace() << "Creating folder: " << folderName << parentFolderRid;
        const auto remoteId = folderPath.toUtf8();
        const auto bufferType = ENTITY_TYPE_FOLDER;
        Sink::ApplicationDomain::Folder folder;
        folder.setProperty(ApplicationDomain::Folder::Name::name, folderName);
        folder.setProperty(ApplicationDomain::Folder::Icon::name, icon);
        QHash<QByteArray, Query::Comparator> mergeCriteria;
        if (SpecialPurpose::isSpecialPurposeFolderName(folderName)) {
            auto type = SpecialPurpose::getSpecialPurposeType(folderName);
            folder.setProperty(ApplicationDomain::Folder::SpecialPurpose::name, QVariant::fromValue(QByteArrayList() << type));
            mergeCriteria.insert(ApplicationDomain::Folder::SpecialPurpose::name, Query::Comparator(type, Query::Comparator::Contains));
        }

        if (!parentFolderRid.isEmpty()) {
            folder.setProperty("parent", syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, parentFolderRid.toUtf8()));
        }
        createOrModify(bufferType, remoteId, folder, mergeCriteria);
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
                    callback(key);
                    return true;
                });
            },
            [&folderList](const QByteArray &remoteId) -> bool {
                // folderList.contains(remoteId)
                for (const auto folderPath : folderList) {
                    if (folderPath.path == remoteId) {
                        return true;
                    }
                }
                return false;
            }
        );

        for (const auto &f : folderList) {
            createFolder(f.pathParts.last(), f.path, f.parentPath(), "folder");
        }
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
            const auto remoteId = assembleMailRid(folderLocalId, message.uid);

            Trace() << "Found a mail " << remoteId << message.msg->subject(true)->asUnicodeString() << message.flags;

            auto mail = Sink::ApplicationDomain::Mail::create(mResourceInstanceIdentifier);
            mail.setFolder(folderLocalId);
            mail.setMimeMessage(message.msg->encodedContent());
            mail.setUnread(!message.flags.contains(Imap::Flags::Seen));
            mail.setImportant(message.flags.contains(Imap::Flags::Flagged));

            createOrModify(bufferType, remoteId, mail);
        }
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
                if (messages.contains(uidFromMailRid(remoteId))) {
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
            Trace() << "Connecting to:" << mServer << mPort;
            Trace() << "as:" << mUser;
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
                if (folder.noselect) {
                    continue;
                }
                QSet<qint64> uids;
                auto messagesFuture = imap.fetchMessages(folder, [this, folder, &uids](const QVector<Message> &messages) {
                    Trace() << "Synchronizing mails" << folder.normalizedPath();
                    for (const auto &msg : messages) {
                        uids << msg.uid;
                    }
                    synchronizeMails(folder.normalizedPath(), messages);
                }).exec();
                messagesFuture.waitForFinished();
                commit();
                if (messagesFuture.errorCode()) {
                    Warning() << "Folder sync failed: " << folder.normalizedPath();
                    continue;
                }
                //Remove what there is to remove
                synchronizeRemovals(folder.normalizedPath(), uids);
                commit();
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

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Mail &mail, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort);
        auto login = imap->login(mUser, mPassword);
        if (operation == Sink::Operation_Creation) {
            QString mailbox = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, mail.getFolder());
            QByteArray content = KMime::LFtoCRLF(mail.getMimeMessage());
            QByteArrayList flags;
            if (!mail.getUnread()) {
                flags << Imap::Flags::Seen;
            }
            if (mail.getImportant()) {
                flags << Imap::Flags::Flagged;
            }
            QDateTime internalDate = mail.getDate();
            auto rid = QSharedPointer<QByteArray>::create();
            return login.then(imap->append(mailbox, content, flags, internalDate))
                .then<void, qint64>([imap, mailbox, rid, mail](qint64 uid) {
                    const auto remoteId = assembleMailRid(mail, uid);
                    //FIXME this get's called after the final error handler? WTF?
                    Trace() << "Finished creating a new mail: " << remoteId;
                    *rid = remoteId;
                }).then<QByteArray>([rid, imap]() { //FIXME fix KJob so we don't need this extra clause
                   return *rid;
                });
        } else if (operation == Sink::Operation_Removal) {
            const auto folderId = folderIdFromMailRid(oldRemoteId);
            const QString mailbox = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, folderId);
            const auto uid = uidFromMailRid(oldRemoteId);
            Trace() << "Removing a mail: " << oldRemoteId << "in the mailbox: " << mailbox;
            KIMAP::ImapSet set;
            set.add(uid);
            return login.then(imap->remove(mailbox, set))
                .then<QByteArray>([imap, oldRemoteId]() {
                    Trace() << "Finished removing a mail: " << oldRemoteId;
                    return QByteArray();
                });
        } else if (operation == Sink::Operation_Modification) {
            const QString mailbox = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, mail.getFolder());
            const auto uid = uidFromMailRid(oldRemoteId);

            Trace() << "Modifying a mail: " << oldRemoteId << " in the mailbox: " << mailbox << changedProperties;

            QByteArrayList flags;
            if (!mail.getUnread()) {
                flags << Imap::Flags::Seen;
            }
            if (mail.getImportant()) {
                flags << Imap::Flags::Flagged;
            }

            const bool messageMoved = changedProperties.contains(ApplicationDomain::Mail::Folder::name);
            const bool messageChanged = changedProperties.contains(ApplicationDomain::Mail::MimeMessage::name);
            if (messageChanged || messageMoved) {
                const auto folderId = folderIdFromMailRid(oldRemoteId);
                const QString oldMailbox = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, folderId);
                QByteArray content = KMime::LFtoCRLF(mail.getMimeMessage());
                QDateTime internalDate = mail.getDate();
                auto rid = QSharedPointer<QByteArray>::create();
                KIMAP::ImapSet set;
                set.add(uid);
                return login.then(imap->append(mailbox, content, flags, internalDate))
                    .then<void, qint64>([imap, mailbox, rid, mail](qint64 uid) {
                        const auto remoteId = assembleMailRid(mail, uid);
                        Trace() << "Finished creating a modified mail: " << remoteId;
                        *rid = remoteId;
                    })
                    .then(imap->remove(oldMailbox, set))
                    .then<QByteArray>([rid, imap]() {
                        return *rid;
                    });
            } else {
                KIMAP::ImapSet set;
                set.add(uid);
                return login.then(imap->select(mailbox))
                    .then(imap->storeFlags(set, flags))
                    .then<void>([imap, mailbox]() {
                        Trace() << "Finished modifying mail";
                    })
                    .then<QByteArray>([oldRemoteId, imap]() {
                        return oldRemoteId;
                    });
            }
        }
        return KAsync::null<QByteArray>();
    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Folder &folder, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort);
        auto login = imap->login(mUser, mPassword);
        if (operation == Sink::Operation_Creation) {
            QString parentFolder;
            if (!folder.getParent().isEmpty()) {
                parentFolder = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, folder.getParent());
            }
            Trace() << "Creating a new folder: " << parentFolder << folder.getName();
            auto rid = QSharedPointer<QByteArray>::create();
            auto createFolder = login.then<QString>(imap->createSubfolder(parentFolder, folder.getName()))
                .then<void, QString>([imap, rid](const QString &createdFolder) {
                    Trace() << "Finished creating a new folder: " << createdFolder;
                    *rid = createdFolder.toUtf8();
                });
            if (folder.getSpecialPurpose().isEmpty()) {
                return createFolder
                    .then<QByteArray>([rid](){
                        return *rid;
                    });
            } else { //We try to merge special purpose folders first
                auto  specialPurposeFolders = QSharedPointer<QHash<QByteArray, QString>>::create();
                auto mergeJob = imap->login(mUser, mPassword)
                    .then<void>(imap->fetchFolders([=](const QVector<Imap::Folder> &folders) {
                        for (const auto &f : folders) {
                            if (SpecialPurpose::isSpecialPurposeFolderName(f.pathParts.last())) {
                                specialPurposeFolders->insert(SpecialPurpose::getSpecialPurposeType(f.pathParts.last()), f.path);
                            };
                        }
                    }))
                    .then<void, KAsync::Job<void>>([specialPurposeFolders, folder, imap, parentFolder, rid]() -> KAsync::Job<void> {
                        for (const auto &purpose : folder.getSpecialPurpose()) {
                            if (specialPurposeFolders->contains(purpose)) {
                                auto f = specialPurposeFolders->value(purpose);
                                Trace() << "Merging specialpurpose folder with: " << f << " with purpose: " << purpose;
                                *rid = f.toUtf8();
                                return KAsync::null<void>();
                            }
                        }
                        Trace() << "No match found for merging, creating a new folder";
                        return imap->createSubfolder(parentFolder, folder.getName())
                            .then<void, QString>([imap, rid](const QString &createdFolder) {
                                Trace() << "Finished creating a new folder: " << createdFolder;
                                *rid = createdFolder.toUtf8();
                            });

                    })
                .then<QByteArray>([rid](){
                    return *rid;
                });
                return mergeJob;
            }
        } else if (operation == Sink::Operation_Removal) {
            Trace() << "Removing a folder: " << oldRemoteId;
            return login.then<void>(imap->remove(oldRemoteId))
                .then<QByteArray>([oldRemoteId, imap]() {
                    Trace() << "Finished removing a folder: " << oldRemoteId;
                    return QByteArray();
                });
        } else if (operation == Sink::Operation_Modification) {
            Trace() << "Renaming a folder: " << oldRemoteId << folder.getName();
            auto rid = QSharedPointer<QByteArray>::create();
            return login.then<QString>(imap->renameSubfolder(oldRemoteId, folder.getName()))
                .then<void, QString>([imap, rid](const QString &createdFolder) {
                    Trace() << "Finished renaming a folder: " << createdFolder;
                    *rid = createdFolder.toUtf8();
                })
                .then<QByteArray>([rid](){
                    return *rid;
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
    mUser = config.value("username").toString();
    mPassword = config.value("password").toString();
    if (mServer.startsWith("imap")) {
        mServer.remove("imap://");
        mServer.remove("imaps://");
    }
    if (mServer.contains(':')) {
        auto list = mServer.split(':');
        mServer = list.at(0);
        mPort = list.at(1).toInt();
    }

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

    setupPreprocessors(ENTITY_TYPE_MAIL, QVector<Sink::Preprocessor*>() << new SpecialPurposeProcessor(mResourceType, mResourceInstanceIdentifier) << new MimeMessageMover << new MailPropertyExtractor << new DefaultIndexUpdater<Sink::ApplicationDomain::Mail>);
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
            Warning() << "Missing remote id for folder or mail. " << mailRemoteId << folderRemoteId;
            return KAsync::error<void>();
        }
        const auto uid = uidFromMailRid(mailRemoteId);
        Trace() << "Mail remote id: " << folderRemoteId << mailRemoteId << mail.identifier() << folder.identifier();

        KIMAP::ImapSet set;
        set.add(uid);
        if (set.isEmpty()) {
            return KAsync::error<void>(1, "Couldn't determine uid of mail.");
        }
        KIMAP::FetchJob::FetchScope scope;
        scope.mode = KIMAP::FetchJob::FetchScope::Full;
        auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort);
        auto messageByUid = QSharedPointer<QHash<qint64, Imap::Message>>::create();
        Trace() << "Connecting to:" << mServer << mPort;
        Trace() << "as:" << mUser;
        auto inspectionJob = imap->login(mUser, mPassword)
            .then<void>(imap->select(folderRemoteId))
            .then<void>(imap->fetch(set, scope, [imap, messageByUid](const QVector<Imap::Message> &messages) {
                for (const auto &m : messages) {
                    messageByUid->insert(m.uid, m);
                }
            }));

        if (inspectionType == Sink::ResourceControl::Inspection::PropertyInspectionType) {
            if (property == "unread") {
                return inspectionJob.then<void, KAsync::Job<void>>([=]() {
                    auto msg = messageByUid->value(uid);
                    if (expectedValue.toBool() && msg.flags.contains(Imap::Flags::Seen)) {
                        return KAsync::error<void>(1, "Expected unread but couldn't find it.");
                    }
                    if (!expectedValue.toBool() && !msg.flags.contains(Imap::Flags::Seen)) {
                        return KAsync::error<void>(1, "Expected read but couldn't find it.");
                    }
                    return KAsync::null<void>();
                });
            }
            if (property == "subject") {
                return inspectionJob.then<void, KAsync::Job<void>>([=]() {
                    auto msg = messageByUid->value(uid);
                    if (msg.msg->subject(true)->asUnicodeString() != expectedValue.toString()) {
                        return KAsync::error<void>(1, "Subject not as expected: " + msg.msg->subject(true)->asUnicodeString());
                    }
                    return KAsync::null<void>();
                });
            }
        }
        if (inspectionType == Sink::ResourceControl::Inspection::ExistenceInspectionType) {
            return inspectionJob.then<void, KAsync::Job<void>>([=]() {
                if (!messageByUid->contains(uid)) {
                    Warning() << "Existing messages are: " << messageByUid->keys();
                    Warning() << "We're looking for: " << uid;
                    return KAsync::error<void>(1, "Couldn't find message: " + mailRemoteId);
                }
                return KAsync::null<void>();
            });
        }
    }
    if (domainType == ENTITY_TYPE_FOLDER) {
        const auto remoteId = syncStore->resolveLocalId(ENTITY_TYPE_FOLDER, entityId);
        const auto folder = entityStore->read<Sink::ApplicationDomain::Folder>(entityId);

        if (inspectionType == Sink::ResourceControl::Inspection::CacheIntegrityInspectionType) {
            Log() << "Inspecting cache integrity" << remoteId;

            int expectedCount = 0;
            Index index("mail.index.folder", transaction);
            index.lookup(entityId, [&](const QByteArray &sinkId) {
                expectedCount++;
            },
            [&](const Index::Error &error) {
                Warning() << "Error in index: " <<  error.message << property;
            });

            auto set = KIMAP::ImapSet::fromImapSequenceSet("1:*");
            KIMAP::FetchJob::FetchScope scope;
            scope.mode = KIMAP::FetchJob::FetchScope::Headers;
            auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort);
            auto messageByUid = QSharedPointer<QHash<qint64, Imap::Message>>::create();
            auto inspectionJob = imap->login(mUser, mPassword)
                .then<void>(imap->select(remoteId))
                .then<void>(imap->fetch(set, scope, [=](const QVector<Imap::Message> &messages) {
                    for (const auto &m : messages) {
                        messageByUid->insert(m.uid, m);
                    }
                }))
                .then<void, KAsync::Job<void>>([imap, messageByUid, expectedCount]() {
                    if (messageByUid->size() != expectedCount) {
                        return KAsync::error<void>(1, QString("Wrong number of messages on the server; found %1 instead of %2.").arg(messageByUid->size()).arg(expectedCount));
                    }
                    return KAsync::null<void>();
                });
            return inspectionJob;
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
