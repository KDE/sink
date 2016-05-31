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
#include "synchronizer.h"
#include "sourcewriteback.h"
#include "adaptorfactoryregistry.h"
#include <QDate>
#include <QUuid>
#include <QDir>
#include <QDirIterator>
#include <KMime/KMime/KMimeMessage>

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

#undef DEBUG_AREA
#define DEBUG_AREA "resource.maildir"

using namespace Sink;

static QString getFilePathFromMimeMessagePath(const QString &mimeMessagePath)
{
    auto parts = mimeMessagePath.split('/');
    const auto key = parts.takeLast();
    const auto path = parts.join("/") + "/cur/";

    QDir dir(path);
    const QFileInfoList list = dir.entryInfoList(QStringList() << (key+"*"), QDir::Files);
    if (list.size() != 1) {
        Warning() << "Failed to find message " << mimeMessagePath;
        Warning() << "Failed to find message " << path;
        return QString();
    }
    return list.first().filePath();
}

class MailPropertyExtractor : public Sink::Preprocessor
{
public:
    MailPropertyExtractor() {}

    void updatedIndexedProperties(Sink::ApplicationDomain::BufferAdaptor &newEntity)
    {
        const auto filePath = getFilePathFromMimeMessagePath(newEntity.getProperty("mimeMessage").toString());

        KMime::Message *msg = new KMime::Message;
        msg->setHead(KMime::CRLFtoLF(KPIM::Maildir::readEntryHeadersFromFile(filePath)));
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

class FolderUpdater : public Sink::Preprocessor
{
public:
    FolderUpdater(const QByteArray &drafts) : mDraftsFolder(drafts) {}

    QString getPath(const QByteArray &folderIdentifier, Sink::Storage::Transaction &transaction)
    {
        if (folderIdentifier.isEmpty()) {
            return mMaildirPath;
        }
        QString folderPath;
        auto db = Sink::Storage::mainDatabase(transaction, ENTITY_TYPE_FOLDER);
        db.findLatest(folderIdentifier, [&](const QByteArray &, const QByteArray &value) {
            Sink::EntityBuffer buffer(value);
            const Sink::Entity &entity = buffer.entity();
            const auto adaptor = Sink::AdaptorFactoryRegistry::instance().getFactory<Sink::ApplicationDomain::Folder>(PLUGIN_NAME)->createAdaptor(entity);
            auto parentFolder = adaptor->getProperty("parent").toString();
            if (mMaildirPath.endsWith(adaptor->getProperty("name").toString())) {
                folderPath = mMaildirPath;
            } else {
                auto folderName = adaptor->getProperty("name").toString();
                //FIXME handle non toplevel folders
                folderPath = mMaildirPath + "/" + folderName;
            }
        });
        return folderPath;
    }

    QString moveMessage(const QString &oldPath, const QByteArray &folder, Sink::Storage::Transaction &transaction)
    {
        if (oldPath.startsWith(Sink::temporaryFileLocation())) {
            const auto path = getPath(folder, transaction);
            KPIM::Maildir maildir(path, false);
            if (!maildir.isValid(true)) {
                qWarning() << "Maildir is not existing: " << path;
            }
            auto identifier = maildir.addEntryFromPath(oldPath);
            return path + "/" + identifier;
        }
        return oldPath;
    }

    void newEntity(const QByteArray &uid, qint64 revision, Sink::ApplicationDomain::BufferAdaptor &newEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        if (newEntity.getProperty("draft").toBool()) {
            newEntity.setProperty("folder", mDraftsFolder);
        }
        const auto mimeMessage = newEntity.getProperty("mimeMessage");
        if (mimeMessage.isValid()) {
            newEntity.setProperty("mimeMessage", moveMessage(mimeMessage.toString(), newEntity.getProperty("folder").toByteArray(), transaction));
        }
    }

    void modifiedEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::ApplicationDomain::BufferAdaptor &newEntity,
        Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        //TODO deal with moves
        const auto mimeMessage = newEntity.getProperty("mimeMessage");
        if (mimeMessage.isValid() && mimeMessage.toString() != oldEntity.getProperty("mimeMessage").toString()) {
            //Remove the olde mime message if there is a new one
            const auto filePath = getFilePathFromMimeMessagePath(oldEntity.getProperty("mimeMessage").toString());
            QFile::remove(filePath);

            newEntity.setProperty("mimeMessage", moveMessage(mimeMessage.toString(), newEntity.getProperty("folder").toByteArray(), transaction));
            Trace() << "Modified message: " << filePath << oldEntity.getProperty("mimeMessage").toString();
        }

        auto mimeMessagePath = newEntity.getProperty("mimeMessage").toString();
        const auto maildirPath = getPath(newEntity.getProperty("folder").toByteArray(), transaction);
        KPIM::Maildir maildir(maildirPath, false);
        QString identifier = KPIM::Maildir::getKeyFromFile(mimeMessagePath);

        //get flags from
        KPIM::Maildir::Flags flags;
        if (!newEntity.getProperty("unread").toBool()) {
            flags |= KPIM::Maildir::Seen;
        }
        if (newEntity.getProperty("important").toBool()) {
            flags |= KPIM::Maildir::Flagged;
        }

        maildir.changeEntryFlags(identifier, flags);
    }

    void deletedEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        const auto filePath = getFilePathFromMimeMessagePath(oldEntity.getProperty("mimeMessage").toString());
        QFile::remove(filePath);
    }
    QByteArray mDraftsFolder;
    QByteArray mResourceInstanceIdentifier;
    QString mMaildirPath;
};

class FolderPreprocessor : public Sink::Preprocessor
{
public:
    FolderPreprocessor() {}

    void newEntity(const QByteArray &uid, qint64 revision, Sink::ApplicationDomain::BufferAdaptor &newEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        auto folderName = newEntity.getProperty("name").toString();
        const auto path = mMaildirPath + "/" + folderName;
        KPIM::Maildir maildir(path, false);
        maildir.create();
    }

    void modifiedEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::ApplicationDomain::BufferAdaptor &newEntity,
        Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
    }

    void deletedEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
    }
    QString mMaildirPath;
};


class MaildirSynchronizer : public Sink::Synchronizer {
public:
    MaildirSynchronizer(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier)
        : Sink::Synchronizer(resourceType, resourceInstanceIdentifier)
    {

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

    QByteArray createFolder(const QString &folderPath, const QByteArray &icon)
    {
        auto remoteId = folderPath.toUtf8();
        auto bufferType = ENTITY_TYPE_FOLDER;
        KPIM::Maildir md(folderPath, folderPath == mMaildirPath);
        Sink::ApplicationDomain::Folder folder;
        folder.setProperty("name", md.name());
        folder.setProperty("icon", icon);

        if (!md.isRoot()) {
            folder.setProperty("parent", syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, md.parent().path().toUtf8()));
        }
        createOrModify(bufferType, remoteId, folder);
        return remoteId;
    }

    QStringList listAvailableFolders()
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

    void synchronizeFolders()
    {
        const QByteArray bufferType = ENTITY_TYPE_FOLDER;
        QStringList folderList = listAvailableFolders();
        Trace() << "Found folders " << folderList;

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
                return folderList.contains(remoteId);
            }
        );

        for (const auto folderPath : folderList) {
            createFolder(folderPath, "folder");
        }
    }

    void synchronizeMails(const QString &path)
    {
        Trace() << "Synchronizing mails" << path;
        auto time = QSharedPointer<QTime>::create();
        time->start();
        const QByteArray bufferType = ENTITY_TYPE_MAIL;

        KPIM::Maildir maildir(path, true);
        if (!maildir.isValid()) {
            Warning() << "Failed to sync folder " << maildir.lastError();
            return;
        }

        Trace() << "Importing new mail.";
        maildir.importNewMails();

        auto listingPath = maildir.pathToCurrent();
        auto entryIterator = QSharedPointer<QDirIterator>::create(listingPath, QDir::Files);
        Trace() << "Looking into " << listingPath;

        const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, path.toUtf8());

        auto property = "folder";
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
            [](const QByteArray &remoteId) -> bool {
                return QFile(remoteId).exists();
            }
        );

        int count = 0;
        while (entryIterator->hasNext()) {
            count++;
            const QString filePath = QDir::fromNativeSeparators(entryIterator->next());
            const QString fileName = entryIterator->fileName();
            const auto remoteId = filePath.toUtf8();

            const auto flags = maildir.readEntryFlags(fileName);
            const auto maildirKey = maildir.getKeyFromFile(fileName);

            Trace() << "Found a mail " << filePath << " : " << fileName;

            Sink::ApplicationDomain::Mail mail;
            mail.setProperty("folder", folderLocalId);
            //We only store the directory path + key, so we facade can add the changing bits (flags)
            mail.setProperty("mimeMessage", KPIM::Maildir::getDirectoryFromFile(filePath) + maildirKey);
            mail.setProperty("unread", !flags.testFlag(KPIM::Maildir::Seen));
            mail.setProperty("important", flags.testFlag(KPIM::Maildir::Flagged));

            createOrModify(bufferType, remoteId, mail);
        }
        commitSync();
        const auto elapsed = time->elapsed();
        Log() << "Synchronized " << count << " mails in " << listingPath << Sink::Log::TraceTime(elapsed) << " " << elapsed/qMax(count, 1) << " [ms/mail]";
    }

    KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE
    {
        Log() << " Synchronizing";
        return KAsync::start<void>([this]() {
            synchronizeFolders();
            //The next sync needs the folders available
            commit();
            commitSync();
            for (const auto &folder : listAvailableFolders()) {
                synchronizeMails(folder);
                //Don't let the transaction grow too much
                commit();
                commitSync();
            }
            Log() << "Done Synchronizing";
        });
    }

public:
    QString mMaildirPath;
};

class MaildirWriteback : public Sink::SourceWriteBack
{
public:
    MaildirWriteback(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier) : Sink::SourceWriteBack(resourceType, resourceInstanceIdentifier)
    {

    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Mail &mail, Sink::Operation operation, const QByteArray &oldRemoteId)
    {
        if (operation == Sink::Operation_Creation) {
            const auto remoteId = getFilePathFromMimeMessagePath(mail.getMimeMessagePath());
            Trace() << "Mail created: " << remoteId;
            return KAsync::start<QByteArray>([=]() -> QByteArray {
                return remoteId.toUtf8();
            });
        } else if (operation == Sink::Operation_Removal) {
            Trace() << "Removing a mail: " << oldRemoteId;
            // QFile::remove(oldRemoteId);
            return KAsync::null<QByteArray>();
        } else if (operation == Sink::Operation_Modification) {
            Trace() << "Modifying a mail: " << oldRemoteId;
            const auto remoteId = getFilePathFromMimeMessagePath(mail.getMimeMessagePath());
            return KAsync::start<QByteArray>([=]() -> QByteArray {
                return remoteId.toUtf8();
            });
        }
        return KAsync::null<QByteArray>();
    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Folder &folder, Sink::Operation operation, const QByteArray &oldRemoteId)
    {
        if (operation == Sink::Operation_Creation) {
            auto folderName = folder.getName();
            //FIXME handle non toplevel folders
            auto path = mMaildirPath + "/" + folderName;
            Trace() << "Creating a new folder: " << path;
            KPIM::Maildir maildir(path, false);
            maildir.create();
            return KAsync::start<QByteArray>([=]() -> QByteArray {
                return path.toUtf8();
            });
        } else if (operation == Sink::Operation_Removal) {
            const auto path = oldRemoteId;
            Trace() << "Removing a folder: " << path;
            KPIM::Maildir maildir(path, false);
            maildir.remove();
            return KAsync::null<QByteArray>();
        } else if (operation == Sink::Operation_Modification) {
            Warning() << "Folder modifications are not implemented";
            return KAsync::start<QByteArray>([=]() -> QByteArray {
                return oldRemoteId;
            });
        }
        return KAsync::null<QByteArray>();
    }

public:
    QString mMaildirPath;
};


MaildirResource::MaildirResource(const QByteArray &instanceIdentifier, const QSharedPointer<Sink::Pipeline> &pipeline)
    : Sink::GenericResource(PLUGIN_NAME, instanceIdentifier, pipeline)
{
    auto config = ResourceConfig::getConfiguration(instanceIdentifier);
    mMaildirPath = QDir::cleanPath(QDir::fromNativeSeparators(config.value("path").toString()));
    //Chop a trailing slash if necessary
    if (mMaildirPath.endsWith("/")) {
        mMaildirPath.chop(1);
    }

    auto synchronizer = QSharedPointer<MaildirSynchronizer>::create(PLUGIN_NAME, instanceIdentifier);
    synchronizer->mMaildirPath = mMaildirPath;
    setupSynchronizer(synchronizer);
    auto changereplay = QSharedPointer<MaildirWriteback>::create(PLUGIN_NAME, instanceIdentifier);
    changereplay->mMaildirPath = mMaildirPath;
    setupChangereplay(changereplay);

    auto folderUpdater = new FolderUpdater(QByteArray());
    setupPreprocessors(ENTITY_TYPE_MAIL, QVector<Sink::Preprocessor*>() << folderUpdater << new MailPropertyExtractor << new DefaultIndexUpdater<Sink::ApplicationDomain::Mail>);
    auto folderPreprocessor = new FolderPreprocessor;
    setupPreprocessors(ENTITY_TYPE_FOLDER, QVector<Sink::Preprocessor*>() << folderPreprocessor << new DefaultIndexUpdater<Sink::ApplicationDomain::Folder>);

    KPIM::Maildir dir(mMaildirPath, true);
    mDraftsFolder = dir.addSubFolder("drafts");
    Trace() << "Started maildir resource for maildir: " << mMaildirPath;

    auto remoteId = synchronizer->createFolder(mDraftsFolder, "folder");
    auto draftsFolderLocalId = synchronizer->syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, remoteId);
    synchronizer->commit();
    synchronizer->commitSync();

    folderUpdater->mDraftsFolder = draftsFolderLocalId;
    folderUpdater->mResourceInstanceIdentifier = mResourceInstanceIdentifier;
    folderUpdater->mMaildirPath = mMaildirPath;
    folderPreprocessor->mMaildirPath = mMaildirPath;
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

    auto mainStore = QSharedPointer<Sink::Storage>::create(Sink::storageLocation(), mResourceInstanceIdentifier, Sink::Storage::ReadOnly);
    auto transaction = mainStore->createTransaction(Sink::Storage::ReadOnly);

    auto entityStore = QSharedPointer<EntityStore>::create(mResourceType, mResourceInstanceIdentifier, transaction);
    auto syncStore = QSharedPointer<RemoteIdMap>::create(synchronizationTransaction);

    Trace() << "Inspecting " << inspectionType << domainType << entityId << property << expectedValue;

    if (domainType == ENTITY_TYPE_MAIL) {
        auto mail = entityStore->read<Sink::ApplicationDomain::Mail>(entityId);
        const auto filePath = getFilePathFromMimeMessagePath(mail.getMimeMessagePath());

        if (inspectionType == Sink::ResourceControl::Inspection::PropertyInspectionType) {
            if (property == "unread") {
                const auto flags = KPIM::Maildir::readEntryFlags(filePath.split('/').last());
                if (expectedValue.toBool() && (flags & KPIM::Maildir::Seen)) {
                    return KAsync::error<void>(1, "Expected unread but couldn't find it.");
                }
                if (!expectedValue.toBool() && !(flags & KPIM::Maildir::Seen)) {
                    return KAsync::error<void>(1, "Expected read but couldn't find it.");
                }
                return KAsync::null<void>();
            }
            if (property == "subject") {
                KMime::Message *msg = new KMime::Message;
                msg->setHead(KMime::CRLFtoLF(KPIM::Maildir::readEntryHeadersFromFile(filePath)));
                msg->parse();

                if (msg->subject(true)->asUnicodeString() != expectedValue.toString()) {
                    return KAsync::error<void>(1, "Subject not as expected: " + msg->subject(true)->asUnicodeString());
                }
                return KAsync::null<void>();
            }
        }
        if (inspectionType == Sink::ResourceControl::Inspection::ExistenceInspectionType) {
            if (QFileInfo(filePath).exists() != expectedValue.toBool()) {
                return KAsync::error<void>(1, "Wrong file existence: " + filePath);
            }
        }
    }
    if (domainType == ENTITY_TYPE_FOLDER) {
        const auto remoteId = syncStore->resolveLocalId(ENTITY_TYPE_FOLDER, entityId);
        auto folder = entityStore->read<Sink::ApplicationDomain::Folder>(entityId);

        if (inspectionType == Sink::ResourceControl::Inspection::CacheIntegrityInspectionType) {
            Trace() << "Inspecting cache integrity" << remoteId;
            if (!QDir(remoteId).exists()) {
                return KAsync::error<void>(1, "The directory is not existing: " + remoteId);
            }

            int expectedCount = 0;
            Index index("mail.index.folder", transaction);
            index.lookup(entityId, [&](const QByteArray &sinkId) {
                    expectedCount++;
            },
            [&](const Index::Error &error) {
                Warning() << "Error in index: " <<  error.message << property;
            });

            QDir dir(remoteId + "/cur");
            const QFileInfoList list = dir.entryInfoList(QDir::Files);
            if (list.size() != expectedCount) {
                for (const auto &fileInfo : list) {
                    Warning() << "Found in cache: " << fileInfo.fileName();
                }
                return KAsync::error<void>(1, QString("Wrong number of files; found %1 instead of %2.").arg(list.size()).arg(expectedCount));
            }
            if (inspectionType == Sink::ResourceControl::Inspection::ExistenceInspectionType) {
                if (!remoteId.endsWith(folder.getName().toUtf8())) {
                    return KAsync::error<void>(1, "Wrong folder name: " + remoteId);
                }
                //TODO we shouldn't use the remoteId here to figure out the path, it could be gone/changed already
                if (QDir(remoteId).exists() != expectedValue.toBool()) {
                    return KAsync::error<void>(1, "Wrong folder existence: " + remoteId);
                }
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

void MaildirResourceFactory::registerAdaptorFactories(Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<Sink::ApplicationDomain::Mail, MaildirMailAdaptorFactory>(PLUGIN_NAME);
    registry.registerFactory<Sink::ApplicationDomain::Folder, MaildirFolderAdaptorFactory>(PLUGIN_NAME);
}

