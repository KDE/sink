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

SINK_DEBUG_AREA("imapresource")

Q_DECLARE_METATYPE(QSharedPointer<Imap::ImapServerProxy>)

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
        SinkTrace() << "Creating folder: " << folderName << parentFolderRid;
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
        SinkTrace() << "Found folders " << folderList.size();

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
                for (const auto &folderPath : folderList) {
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

        SinkTrace() << "Importing new mail." << path;

        const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, path.toUtf8());

        int count = 0;
        for (const auto &message : messages) {
            count++;
            const auto remoteId = assembleMailRid(folderLocalId, message.uid);

            SinkTrace() << "Found a mail " << remoteId << message.msg->subject(true)->asUnicodeString() << message.flags;

            auto mail = Sink::ApplicationDomain::Mail::create(mResourceInstanceIdentifier);
            mail.setFolder(folderLocalId);
            mail.setMimeMessage(message.msg->encodedContent());
            mail.setUnread(!message.flags.contains(Imap::Flags::Seen));
            mail.setImportant(message.flags.contains(Imap::Flags::Flagged));

            createOrModify(bufferType, remoteId, mail);
        }
        const auto elapsed = time->elapsed();
        SinkTrace() << "Synchronized " << count << " mails in " << path << Sink::Log::TraceTime(elapsed) << " " << elapsed/qMax(count, 1) << " [ms/mail]";
    }

    void synchronizeRemovals(const QString &path, const QSet<qint64> &messages)
    {
        auto time = QSharedPointer<QTime>::create();
        time->start();
        const QByteArray bufferType = ENTITY_TYPE_MAIL;

        SinkTrace() << "Finding removed mail.";

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
                    SinkWarning() << "Error in index: " <<  error.message << property;
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
        SinkLog() << "Removed " << count << " mails in " << path << Sink::Log::TraceTime(elapsed) << " " << elapsed/qMax(count, 1) << " [ms/mail]";
    }

    KAsync::Job<void> synchronizeFolder(QSharedPointer<ImapServerProxy> imap, const Imap::Folder &folder)
    {
        QSet<qint64> uids;
        SinkLog() << "Synchronizing mails" << folder.normalizedPath();
        auto capabilities = imap->getCapabilities();
        bool canDoIncrementalRemovals = false;
        return KAsync::start<void>([=]() {
            auto uidNext = syncStore().readValue(folder.normalizedPath().toUtf8() + "uidnext").toLongLong();
            const auto changedsince = syncStore().readValue(folder.normalizedPath().toUtf8() + "changedsince").toLongLong();
            return imap->fetchFlags(folder, KIMAP2::ImapSet(1, qMax(uidNext, qint64(1))), changedsince, [this, folder](const QVector<Message> &messages) {
                // synchronizeMails(folder.normalizedPath(), messages);
                const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, folder.normalizedPath().toUtf8());
                for (const auto &message : messages) {
                    const auto remoteId = assembleMailRid(folderLocalId, message.uid);


                    auto mail = Sink::ApplicationDomain::Mail::create(mResourceInstanceIdentifier);
                    mail.setUnread(!message.flags.contains(Imap::Flags::Seen));
                    mail.setImportant(message.flags.contains(Imap::Flags::Flagged));

                    modify(ENTITY_TYPE_MAIL, remoteId, mail);
                }
            })
            .syncThen<void, SelectResult>([this, folder](const SelectResult &selectResult) {
                syncStore().writeValue(folder.normalizedPath().toUtf8() + "changedsince", QByteArray::number(selectResult.highestModSequence));
            });
        })
        .then<void>([=]() {
            //TODO Remove what's no longer existing
            if (canDoIncrementalRemovals) {
            } else {
                return imap->fetchUids(folder).syncThen<void, QVector<qint64>>([this, folder](const QVector<qint64> &uids) {
                    SinkTrace() << "Syncing removals";
                    synchronizeRemovals(folder.normalizedPath(), uids.toList().toSet());
                    commit();
                });
            }
            return KAsync::null<void>();
        })
        .then<void>([this, folder, imap]() {
            SinkTrace() << "About to fetch mail";
            const auto uidNext = syncStore().readValue(folder.normalizedPath().toUtf8() + "uidnext").toLongLong();
            auto maxUid = QSharedPointer<qint64>::create(0);
            return imap->fetchMessages(folder, uidNext, [this, folder, maxUid](const QVector<Message> &messages) {
                SinkTrace() << "Got mail";
                for (const auto &m : messages) {
                    if (*maxUid < m.uid) {
                        *maxUid = m.uid;
                    }
                }
                synchronizeMails(folder.normalizedPath(), messages);
            },
            [this, maxUid, folder](int progress, int total) {
                SinkLog() << "Progress: " << progress << " out of " << total;
                //commit every 10 messages
                if ((progress % 10) == 0) {
                    commit();
                }
            })
            .syncThen<void>([this, maxUid, folder]() {
                syncStore().writeValue(folder.normalizedPath().toUtf8() + "uidnext", QByteArray::number(*maxUid));
                SinkLog() << "UIDMAX: " << *maxUid << folder.normalizedPath();
                commit();
            });
        });


    }

    KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE
    {
        SinkLog() << " Synchronizing";
        return KAsync::start<void>([this]() {
            SinkTrace() << "Connecting to:" << mServer << mPort;
            SinkTrace() << "as:" << mUser;
            auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort);

            return imap->login(mUser, mPassword)
            .addToContext(imap)
            .onError([](const KAsync::Error &error) {
                SinkWarning() << "Login failed.";
            })
            .then<QVector<Folder>>([this, imap]() {
                auto folderList = QSharedPointer<QVector<Folder>>::create();
                SinkLog() << "Login was successful";
                return  imap->fetchFolders([folderList](const Folder &folder) {
                    *folderList << folder;
                })
                .onError([](const KAsync::Error &error) {
                    SinkWarning() << "Folder list sync failed.";
                })
                .syncThen<QVector<Folder>>([this, folderList]() {
                    synchronizeFolders(*folderList);
                    commit();
                    return *folderList;
                });
            })
            .serialEach<void>([this, imap](const Folder &folder) {
                if (folder.noselect) {
                    return KAsync::null<void>();
                }
                return synchronizeFolder(imap, folder);
            });
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
            return login.then(imap->append(mailbox, content, flags, internalDate))
                .addToContext(imap)
                .syncThen<QByteArray, qint64>([mail](qint64 uid) {
                    const auto remoteId = assembleMailRid(mail, uid);
                    SinkTrace() << "Finished creating a new mail: " << remoteId;
                    return remoteId;
                });
        } else if (operation == Sink::Operation_Removal) {
            const auto folderId = folderIdFromMailRid(oldRemoteId);
            const QString mailbox = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, folderId);
            const auto uid = uidFromMailRid(oldRemoteId);
            SinkTrace() << "Removing a mail: " << oldRemoteId << "in the mailbox: " << mailbox;
            KIMAP2::ImapSet set;
            set.add(uid);
            return login.then(imap->remove(mailbox, set))
                .syncThen<QByteArray>([imap, oldRemoteId] {
                    SinkTrace() << "Finished removing a mail: " << oldRemoteId;
                    return QByteArray();
                });
        } else if (operation == Sink::Operation_Modification) {
            const QString mailbox = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, mail.getFolder());
            const auto uid = uidFromMailRid(oldRemoteId);

            SinkTrace() << "Modifying a mail: " << oldRemoteId << " in the mailbox: " << mailbox << changedProperties;

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
                SinkTrace() << "Replacing message.";
                const auto folderId = folderIdFromMailRid(oldRemoteId);
                const QString oldMailbox = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, folderId);
                QByteArray content = KMime::LFtoCRLF(mail.getMimeMessage());
                QDateTime internalDate = mail.getDate();
                KIMAP2::ImapSet set;
                set.add(uid);
                return login.then(imap->append(mailbox, content, flags, internalDate))
                    .addToContext(imap)
                    .then<QByteArray, qint64>([=](qint64 uid) {
                        const auto remoteId = assembleMailRid(mail, uid);
                        SinkTrace() << "Finished creating a modified mail: " << remoteId;
                        return imap->remove(oldMailbox, set).then(KAsync::value(remoteId));
                    });
            } else {
                SinkTrace() << "Updating flags only.";
                KIMAP2::ImapSet set;
                set.add(uid);
                return login.then(imap->select(mailbox))
                    .addToContext(imap)
                    .then(imap->storeFlags(set, flags))
                    .syncThen<QByteArray>([=] {
                        SinkTrace() << "Finished modifying mail";
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
            SinkTrace() << "Creating a new folder: " << parentFolder << folder.getName();
            auto rid = QSharedPointer<QByteArray>::create();
            auto createFolder = login.then<QString>(imap->createSubfolder(parentFolder, folder.getName()))
                .syncThen<void, QString>([imap, rid](const QString &createdFolder) {
                    SinkTrace() << "Finished creating a new folder: " << createdFolder;
                    *rid = createdFolder.toUtf8();
                });
            if (folder.getSpecialPurpose().isEmpty()) {
                return createFolder
                    .syncThen<QByteArray>([rid](){
                        return *rid;
                    });
            } else { //We try to merge special purpose folders first
                auto  specialPurposeFolders = QSharedPointer<QHash<QByteArray, QString>>::create();
                auto mergeJob = imap->login(mUser, mPassword)
                    .then<void>(imap->fetchFolders([=](const Imap::Folder &folder) {
                        if (SpecialPurpose::isSpecialPurposeFolderName(folder.pathParts.last())) {
                            specialPurposeFolders->insert(SpecialPurpose::getSpecialPurposeType(folder.pathParts.last()), folder.path);
                        };
                    }))
                    .then<void>([specialPurposeFolders, folder, imap, parentFolder, rid]() -> KAsync::Job<void> {
                        for (const auto &purpose : folder.getSpecialPurpose()) {
                            if (specialPurposeFolders->contains(purpose)) {
                                auto f = specialPurposeFolders->value(purpose);
                                SinkTrace() << "Merging specialpurpose folder with: " << f << " with purpose: " << purpose;
                                *rid = f.toUtf8();
                                return KAsync::null<void>();
                            }
                        }
                        SinkTrace() << "No match found for merging, creating a new folder";
                        return imap->createSubfolder(parentFolder, folder.getName())
                            .syncThen<void, QString>([imap, rid](const QString &createdFolder) {
                                SinkTrace() << "Finished creating a new folder: " << createdFolder;
                                *rid = createdFolder.toUtf8();
                            });

                    })
                .syncThen<QByteArray>([rid](){
                    return *rid;
                });
                return mergeJob;
            }
        } else if (operation == Sink::Operation_Removal) {
            SinkTrace() << "Removing a folder: " << oldRemoteId;
            return login.then<void>(imap->remove(oldRemoteId))
                .syncThen<QByteArray>([oldRemoteId, imap]() {
                    SinkTrace() << "Finished removing a folder: " << oldRemoteId;
                    return QByteArray();
                });
        } else if (operation == Sink::Operation_Modification) {
            SinkTrace() << "Renaming a folder: " << oldRemoteId << folder.getName();
            auto rid = QSharedPointer<QByteArray>::create();
            return login.then<QString>(imap->renameSubfolder(oldRemoteId, folder.getName()))
                .syncThen<void, QString>([imap, rid](const QString &createdFolder) {
                    SinkTrace() << "Finished renaming a folder: " << createdFolder;
                    *rid = createdFolder.toUtf8();
                })
                .syncThen<QByteArray>([rid](){
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

    SinkTrace() << "Inspecting " << inspectionType << domainType << entityId << property << expectedValue;

    if (domainType == ENTITY_TYPE_MAIL) {
        const auto mail = entityStore->read<Sink::ApplicationDomain::Mail>(entityId);
        const auto folder = entityStore->read<Sink::ApplicationDomain::Folder>(mail.getFolder());
        const auto folderRemoteId = syncStore->resolveLocalId(ENTITY_TYPE_FOLDER, mail.getFolder());
        const auto mailRemoteId = syncStore->resolveLocalId(ENTITY_TYPE_MAIL, mail.identifier());
        if (mailRemoteId.isEmpty() || folderRemoteId.isEmpty()) {
            SinkWarning() << "Missing remote id for folder or mail. " << mailRemoteId << folderRemoteId;
            return KAsync::error<void>();
        }
        const auto uid = uidFromMailRid(mailRemoteId);
        SinkTrace() << "Mail remote id: " << folderRemoteId << mailRemoteId << mail.identifier() << folder.identifier();

        KIMAP2::ImapSet set;
        set.add(uid);
        if (set.isEmpty()) {
            return KAsync::error<void>(1, "Couldn't determine uid of mail.");
        }
        KIMAP2::FetchJob::FetchScope scope;
        scope.mode = KIMAP2::FetchJob::FetchScope::Full;
        auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort);
        auto messageByUid = QSharedPointer<QHash<qint64, Imap::Message>>::create();
        SinkTrace() << "Connecting to:" << mServer << mPort;
        SinkTrace() << "as:" << mUser;
        auto inspectionJob = imap->login(mUser, mPassword)
            .then<Imap::SelectResult>(imap->select(folderRemoteId))
            .syncThen<void, Imap::SelectResult>([](Imap::SelectResult){})
            .then<void>(imap->fetch(set, scope, [imap, messageByUid](const QVector<Imap::Message> &messages) {
                for (const auto &m : messages) {
                    messageByUid->insert(m.uid, m);
                }
            }));

        if (inspectionType == Sink::ResourceControl::Inspection::PropertyInspectionType) {
            if (property == "unread") {
                return inspectionJob.then<void>([=]() {
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
                return inspectionJob.then<void>([=]() {
                    auto msg = messageByUid->value(uid);
                    if (msg.msg->subject(true)->asUnicodeString() != expectedValue.toString()) {
                        return KAsync::error<void>(1, "Subject not as expected: " + msg.msg->subject(true)->asUnicodeString());
                    }
                    return KAsync::null<void>();
                });
            }
        }
        if (inspectionType == Sink::ResourceControl::Inspection::ExistenceInspectionType) {
            return inspectionJob.then<void>([=]() {
                if (!messageByUid->contains(uid)) {
                    SinkWarning() << "Existing messages are: " << messageByUid->keys();
                    SinkWarning() << "We're looking for: " << uid;
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
            SinkLog() << "Inspecting cache integrity" << remoteId;

            int expectedCount = 0;
            Index index("mail.index.folder", transaction);
            index.lookup(entityId, [&](const QByteArray &sinkId) {
                expectedCount++;
            },
            [&](const Index::Error &error) {
                SinkWarning() << "Error in index: " <<  error.message << property;
            });

            auto set = KIMAP2::ImapSet::fromImapSequenceSet("1:*");
            KIMAP2::FetchJob::FetchScope scope;
            scope.mode = KIMAP2::FetchJob::FetchScope::Headers;
            auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort);
            auto messageByUid = QSharedPointer<QHash<qint64, Imap::Message>>::create();
            return imap->login(mUser, mPassword)
                .then<void>(imap->select(remoteId).syncThen<void>([](){}))
                .then<void>(imap->fetch(set, scope, [=](const QVector<Imap::Message> &messages) {
                    for (const auto &m : messages) {
                        messageByUid->insert(m.uid, m);
                    }
                }))
                .then<void>([imap, messageByUid, expectedCount]() {
                    if (messageByUid->size() != expectedCount) {
                        return KAsync::error<void>(1, QString("Wrong number of messages on the server; found %1 instead of %2.").arg(messageByUid->size()).arg(expectedCount));
                    }
                    return KAsync::null<void>();
                });
        }
        if (inspectionType == Sink::ResourceControl::Inspection::ExistenceInspectionType) {
            auto  folderByPath = QSharedPointer<QSet<QString>>::create();
            auto  folderByName = QSharedPointer<QSet<QString>>::create();

            auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort);
            auto inspectionJob = imap->login(mUser, mPassword)
                .then<void>(imap->fetchFolders([=](const Imap::Folder &f) {
                    *folderByPath << f.normalizedPath();
                    *folderByName << f.pathParts.last();
                }))
                .then<void>([this, folderByName, folderByPath, folder, remoteId, imap]() {
                    if (!folderByName->contains(folder.getName())) {
                        SinkWarning() << "Existing folders are: " << *folderByPath;
                        SinkWarning() << "We're looking for: " << folder.getName();
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
