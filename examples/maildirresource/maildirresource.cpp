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
#include "resourceconfig.h"
#include "index.h"
#include "log.h"
#include "definitions.h"
#include "libmaildir/maildir.h"
#include "inspection.h"
#include "synchronizer.h"
#include "inspector.h"

#include "facadefactory.h"
#include "adaptorfactoryregistry.h"

#include "mailpreprocessor.h"
#include "specialpurposepreprocessor.h"

#include <QDir>
#include <QDirIterator>
#include <KMime/KMime/KMimeMessage>

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

SINK_DEBUG_AREA("maildirresource")

using namespace Sink;

static QString getFilePathFromMimeMessagePath(const QString &mimeMessagePath)
{
    auto parts = mimeMessagePath.split('/');
    const auto key = parts.takeLast();
    const auto path = parts.join("/") + "/cur/";

    QDir dir(path);
    const QFileInfoList list = dir.entryInfoList(QStringList() << (key+"*"), QDir::Files);
    if (list.size() != 1) {
        SinkWarning() << "Failed to find message " << mimeMessagePath;
        SinkWarning() << "Failed to find message " << path;
        return QString();
    }
    return list.first().filePath();
}

class MaildirMailPropertyExtractor : public MailPropertyExtractor
{
protected:
    virtual QString getFilePathFromMimeMessagePath(const QString &mimeMessagePath) const Q_DECL_OVERRIDE
    {
        return ::getFilePathFromMimeMessagePath(mimeMessagePath);
    }
};

class MaildirMimeMessageMover : public Sink::Preprocessor
{
public:
    MaildirMimeMessageMover(const QByteArray &resourceInstanceIdentifier, const QString &maildirPath) : mResourceInstanceIdentifier(resourceInstanceIdentifier), mMaildirPath(maildirPath) {}

    QString getPath(const QByteArray &folderIdentifier)
    {
        if (folderIdentifier.isEmpty()) {
            return mMaildirPath;
        }
        QString folderPath;
        const auto folder = entityStore().readLatest<ApplicationDomain::Folder>(folderIdentifier);
        if (mMaildirPath.endsWith(folder.getName())) {
            folderPath = mMaildirPath;
        } else {
            auto folderName = folder.getName();
            //FIXME handle non toplevel folders
            folderPath = mMaildirPath + "/" + folderName;
        }
        return folderPath;
    }

    QString moveMessage(const QString &oldPath, const QByteArray &folder)
    {
        if (oldPath.startsWith(Sink::temporaryFileLocation())) {
            const auto path = getPath(folder);
            KPIM::Maildir maildir(path, false);
            if (!maildir.isValid(true)) {
                SinkWarning() << "Maildir is not existing: " << path;
            }
            auto identifier = maildir.addEntryFromPath(oldPath);
            return path + "/" + identifier;
        } else {
            //Handle moves
            const auto path = getPath(folder);
            KPIM::Maildir maildir(path, false);
            if (!maildir.isValid(true)) {
                SinkWarning() << "Maildir is not existing: " << path;
            }
            auto oldIdentifier = KPIM::Maildir::getKeyFromFile(oldPath);
            auto pathParts = oldPath.split('/');
            pathParts.takeLast();
            auto oldDirectory = pathParts.join('/');
            if (oldDirectory == path) {
                return oldPath;
            }
            KPIM::Maildir oldMaildir(oldDirectory, false);
            if (!oldMaildir.isValid(false)) {
                SinkWarning() << "Maildir is not existing: " << path;
            }
            auto identifier = oldMaildir.moveEntryTo(oldIdentifier, maildir);
            return path + "/" + identifier;
        }
    }

    void newEntity(Sink::ApplicationDomain::ApplicationDomainType &newEntity) Q_DECL_OVERRIDE
    {
        auto mail = newEntity.cast<ApplicationDomain::Mail>();
        const auto mimeMessage = mail.getMimeMessagePath();
        if (!mimeMessage.isNull()) {
            const auto path = moveMessage(mimeMessage, mail.getFolder());
            auto blob = ApplicationDomain::BLOB{path};
            blob.isExternal = false;
            mail.setProperty(ApplicationDomain::Mail::MimeMessage::name, QVariant::fromValue(blob));
        }
    }

    void modifiedEntity(const Sink::ApplicationDomain::ApplicationDomainType &oldEntity, Sink::ApplicationDomain::ApplicationDomainType &newEntity) Q_DECL_OVERRIDE
    {
        auto newMail = newEntity.cast<ApplicationDomain::Mail>();
        const ApplicationDomain::Mail oldMail{oldEntity};
        const auto mimeMessage = newMail.getMimeMessagePath();
        const auto newFolder = newMail.getFolder();
        const bool mimeMessageChanged = !mimeMessage.isNull() && mimeMessage != oldMail.getMimeMessagePath();
        const bool folderChanged = !newFolder.isNull() && newFolder != oldMail.getFolder();
        if (mimeMessageChanged || folderChanged) {
            SinkTrace() << "Moving mime message: " << mimeMessageChanged << folderChanged;
            auto newPath = moveMessage(mimeMessage, newMail.getFolder());
            if (newPath != oldMail.getMimeMessagePath()) {
                const auto oldPath = getFilePathFromMimeMessagePath(oldMail.getMimeMessagePath());
                auto blob = ApplicationDomain::BLOB{newPath};
                blob.isExternal = false;
                newMail.setProperty(ApplicationDomain::Mail::MimeMessage::name, QVariant::fromValue(blob));
                //Remove the olde mime message if there is a new one
                QFile::remove(oldPath);
            }
        }

        auto mimeMessagePath = newMail.getMimeMessagePath();
        const auto maildirPath = getPath(newMail.getFolder());
        KPIM::Maildir maildir(maildirPath, false);
        const auto file = getFilePathFromMimeMessagePath(mimeMessagePath);
        QString identifier = KPIM::Maildir::getKeyFromFile(file);

        //get flags from
        KPIM::Maildir::Flags flags;
        if (!newMail.getUnread()) {
            flags |= KPIM::Maildir::Seen;
        }
        if (newMail.getImportant()) {
            flags |= KPIM::Maildir::Flagged;
        }

        maildir.changeEntryFlags(identifier, flags);
    }

    void deletedEntity(const Sink::ApplicationDomain::ApplicationDomainType &oldEntity) Q_DECL_OVERRIDE
    {
        const ApplicationDomain::Mail oldMail{oldEntity};
        const auto filePath = getFilePathFromMimeMessagePath(oldMail.getMimeMessagePath());
        QFile::remove(filePath);
    }
    QByteArray mResourceInstanceIdentifier;
    QString mMaildirPath;
};

class FolderPreprocessor : public Sink::Preprocessor
{
public:
    FolderPreprocessor(const QString maildirPath) : mMaildirPath(maildirPath) {}

    void newEntity(Sink::ApplicationDomain::ApplicationDomainType &newEntity) Q_DECL_OVERRIDE
    {
        auto folderName = Sink::ApplicationDomain::Folder{newEntity}.getName();
        const auto path = mMaildirPath + "/" + folderName;
        KPIM::Maildir maildir(path, false);
        maildir.create();
    }

    void modifiedEntity(const Sink::ApplicationDomain::ApplicationDomainType &oldEntity, Sink::ApplicationDomain::ApplicationDomainType &newEntity) Q_DECL_OVERRIDE
    {
    }

    void deletedEntity(const Sink::ApplicationDomain::ApplicationDomainType &oldEntity) Q_DECL_OVERRIDE
    {
    }
    QString mMaildirPath;
};


class MaildirSynchronizer : public Sink::Synchronizer {
public:
    MaildirSynchronizer(const Sink::ResourceContext &resourceContext)
        : Sink::Synchronizer(resourceContext)
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

    QByteArray createFolder(const QString &folderPath, const QByteArray &icon, const QByteArrayList &specialpurpose = QByteArrayList())
    {
        auto remoteId = folderPath.toUtf8();
        auto bufferType = ENTITY_TYPE_FOLDER;
        KPIM::Maildir md(folderPath, folderPath == mMaildirPath);
        Sink::ApplicationDomain::Folder folder;
        folder.setName(md.name());
        folder.setIcon(icon);
        if (!specialpurpose.isEmpty()) {
            folder.setSpecialPurpose(specialpurpose);
        }

        if (!md.isRoot()) {
            folder.setParent(syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, md.parent().path().toUtf8()));
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
        SinkTrace() << "Found folders " << folderList;
        scanForRemovals(bufferType,
            [&folderList](const QByteArray &remoteId) -> bool {
                return folderList.contains(remoteId);
            }
        );

        for (const auto &folderPath : folderList) {
            createFolder(folderPath, "folder");
        }
    }

    void synchronizeMails(const QString &path)
    {
        SinkTrace() << "Synchronizing mails" << path;
        auto time = QSharedPointer<QTime>::create();
        time->start();
        const QByteArray bufferType = ENTITY_TYPE_MAIL;

        KPIM::Maildir maildir(path, true);
        if (!maildir.isValid()) {
            SinkWarning() << "Failed to sync folder.";
            return;
        }

        SinkTrace() << "Importing new mail.";
        maildir.importNewMails();

        auto listingPath = maildir.pathToCurrent();
        auto entryIterator = QSharedPointer<QDirIterator>::create(listingPath, QDir::Files);
        SinkTrace() << "Looking into " << listingPath;

        const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, path.toUtf8());

        scanForRemovals(bufferType,
            [&](const std::function<void(const QByteArray &)> &callback) {
                store().indexLookup<ApplicationDomain::Mail, ApplicationDomain::Mail::Folder>(folderLocalId, callback);
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

            SinkTrace() << "Found a mail " << filePath << " : " << fileName;

            Sink::ApplicationDomain::Mail mail;
            mail.setFolder(folderLocalId);
            //We only store the directory path + key, so we facade can add the changing bits (flags)
            auto path = KPIM::Maildir::getDirectoryFromFile(filePath) + maildirKey;
            auto blob = ApplicationDomain::BLOB{path};
            blob.isExternal = false;
            mail.setProperty(ApplicationDomain::Mail::MimeMessage::name, QVariant::fromValue(blob));
            mail.setUnread(!flags.testFlag(KPIM::Maildir::Seen));
            mail.setImportant(flags.testFlag(KPIM::Maildir::Flagged));

            createOrModify(bufferType, remoteId, mail);
        }
        const auto elapsed = time->elapsed();
        SinkLog() << "Synchronized " << count << " mails in " << listingPath << Sink::Log::TraceTime(elapsed) << " " << elapsed/qMax(count, 1) << " [ms/mail]";
    }

    QList<Synchronizer::SyncRequest> getSyncRequests(const Sink::QueryBase &query) Q_DECL_OVERRIDE
    {
        QList<Synchronizer::SyncRequest> list;
        if (!query.type().isEmpty()) {
            //We want to synchronize something specific
            list << Synchronizer::SyncRequest{query};
        } else {
            //We want to synchronize everything
            list << Synchronizer::SyncRequest{Sink::QueryBase(ApplicationDomain::getTypeName<ApplicationDomain::Folder>())};
            //FIXME we can't process the second synchronization before the pipeline of the first one is processed, otherwise we can't execute a query on the local data.
            /* list << Synchronizer::SyncRequest{Flush}; */
            list << Synchronizer::SyncRequest{Sink::QueryBase(ApplicationDomain::getTypeName<ApplicationDomain::Mail>())};
        }
        return list;
    }

    KAsync::Job<void> synchronizeWithSource(const Sink::QueryBase &query) Q_DECL_OVERRIDE
    {
        auto job = KAsync::start<void>([this] {
            KPIM::Maildir maildir(mMaildirPath, true);
            if (!maildir.isValid(false)) {
                return KAsync::error<void>(1, "Maildir path doesn't point to a valid maildir: " + mMaildirPath);
            }
            return KAsync::null<void>();
        });

        if (query.type() == ApplicationDomain::getTypeName<ApplicationDomain::Folder>()) {
            job = job.syncThen<void>([this] {
                synchronizeFolders();
            });
        } else if (query.type() == ApplicationDomain::getTypeName<ApplicationDomain::Mail>()) {
            job = job.syncThen<void>([this, query] {
                QStringList folders;
                if (query.hasFilter<ApplicationDomain::Mail::Folder>()) {
                    auto folderFilter = query.getFilter<ApplicationDomain::Mail::Folder>();
                    auto localIds = resolveFilter(folderFilter);
                    auto folderRemoteIds = syncStore().resolveLocalIds(ApplicationDomain::getTypeName<ApplicationDomain::Folder>(), localIds);
                    for (const auto &r : folderRemoteIds) {
                        folders << r;
                    }
                } else {
                    folders = listAvailableFolders();
                }
                for (const auto &folder : folders) {
                    synchronizeMails(folder);
                    //Don't let the transaction grow too much
                    commit();
                }
            });
        }
        return job;
    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Mail &mail, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        if (operation == Sink::Operation_Creation) {
            const auto remoteId = getFilePathFromMimeMessagePath(mail.getMimeMessagePath());
            SinkTrace() << "Mail created: " << remoteId;
            return KAsync::value(remoteId.toUtf8());
        } else if (operation == Sink::Operation_Removal) {
            SinkTrace() << "Removing a mail: " << oldRemoteId;
            return KAsync::null<QByteArray>();
        } else if (operation == Sink::Operation_Modification) {
            SinkTrace() << "Modifying a mail: " << oldRemoteId;
            const auto remoteId = getFilePathFromMimeMessagePath(mail.getMimeMessagePath());
            return KAsync::value(remoteId.toUtf8());
        }
        return KAsync::null<QByteArray>();
    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Folder &folder, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        if (operation == Sink::Operation_Creation) {
            auto folderName = folder.getName();
            //FIXME handle non toplevel folders
            auto path = mMaildirPath + "/" + folderName;
            SinkTrace() << "Creating a new folder: " << path;
            KPIM::Maildir maildir(path, false);
            maildir.create();
            return KAsync::value(path.toUtf8());
        } else if (operation == Sink::Operation_Removal) {
            const auto path = oldRemoteId;
            SinkTrace() << "Removing a folder: " << path;
            KPIM::Maildir maildir(path, false);
            maildir.remove();
            return KAsync::null<QByteArray>();
        } else if (operation == Sink::Operation_Modification) {
            SinkWarning() << "Folder modifications are not implemented";
            return KAsync::value(oldRemoteId);
        }
        return KAsync::null<QByteArray>();
    }

public:
    QString mMaildirPath;
};

class MaildirInspector : public Sink::Inspector {
public:
    MaildirInspector(const Sink::ResourceContext &resourceContext)
        : Sink::Inspector(resourceContext)
    {

    }
protected:

    KAsync::Job<void> inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue) Q_DECL_OVERRIDE {
        auto synchronizationStore = QSharedPointer<Sink::Storage::DataStore>::create(Sink::storageLocation(), mResourceContext.instanceId() + ".synchronization", Sink::Storage::DataStore::ReadOnly);
        auto synchronizationTransaction = synchronizationStore->createTransaction(Sink::Storage::DataStore::ReadOnly);

        auto mainStore = QSharedPointer<Sink::Storage::DataStore>::create(Sink::storageLocation(), mResourceContext.instanceId(), Sink::Storage::DataStore::ReadOnly);
        auto transaction = mainStore->createTransaction(Sink::Storage::DataStore::ReadOnly);

        Sink::Storage::EntityStore entityStore(mResourceContext);
        auto syncStore = QSharedPointer<SynchronizerStore>::create(synchronizationTransaction);

        SinkTrace() << "Inspecting " << inspectionType << domainType << entityId << property << expectedValue;

        if (domainType == ENTITY_TYPE_MAIL) {
            auto mail = entityStore.readLatest<Sink::ApplicationDomain::Mail>(entityId);
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
            auto folder = entityStore.readLatest<Sink::ApplicationDomain::Folder>(entityId);

            if (inspectionType == Sink::ResourceControl::Inspection::CacheIntegrityInspectionType) {
                SinkTrace() << "Inspecting cache integrity" << remoteId;
                if (!QDir(remoteId).exists()) {
                    return KAsync::error<void>(1, "The directory is not existing: " + remoteId);
                }

                int expectedCount = 0;
                Index index("mail.index.folder", transaction);
                index.lookup(entityId, [&](const QByteArray &sinkId) {
                        expectedCount++;
                },
                [&](const Index::Error &error) {
                    SinkWarning() << "Error in index: " <<  error.message << property;
                });

                QDir dir(remoteId + "/cur");
                const QFileInfoList list = dir.entryInfoList(QDir::Files);
                if (list.size() != expectedCount) {
                    for (const auto &fileInfo : list) {
                        SinkWarning() << "Found in cache: " << fileInfo.fileName();
                    }
                    return KAsync::error<void>(1, QString("Wrong number of files; found %1 instead of %2.").arg(list.size()).arg(expectedCount));
                }
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
        return KAsync::null<void>();
    }
};


MaildirResource::MaildirResource(const Sink::ResourceContext &resourceContext)
    : Sink::GenericResource(resourceContext)
{
    auto config = ResourceConfig::getConfiguration(resourceContext.instanceId());
    mMaildirPath = QDir::cleanPath(QDir::fromNativeSeparators(config.value("path").toString()));
    //Chop a trailing slash if necessary
    if (mMaildirPath.endsWith("/")) {
        mMaildirPath.chop(1);
    }

    auto synchronizer = QSharedPointer<MaildirSynchronizer>::create(resourceContext);
    synchronizer->mMaildirPath = mMaildirPath;
    setupSynchronizer(synchronizer);
    setupInspector(QSharedPointer<MaildirInspector>::create(resourceContext));

    setupPreprocessors(ENTITY_TYPE_MAIL, QVector<Sink::Preprocessor*>() << new SpecialPurposeProcessor(resourceContext.resourceType, resourceContext.instanceId()) << new MaildirMimeMessageMover(resourceContext.instanceId(), mMaildirPath) << new MaildirMailPropertyExtractor);
    setupPreprocessors(ENTITY_TYPE_FOLDER, QVector<Sink::Preprocessor*>() << new FolderPreprocessor(mMaildirPath));

    KPIM::Maildir dir(mMaildirPath, true);
    SinkTrace() << "Started maildir resource for maildir: " << mMaildirPath;
    {
        auto draftsFolder = dir.addSubFolder("Drafts");
        auto remoteId = synchronizer->createFolder(draftsFolder, "folder", QByteArrayList() << "drafts");
        auto draftsFolderLocalId = synchronizer->syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, remoteId);
    }
    {
        auto trashFolder = dir.addSubFolder("Trash");
        auto remoteId = synchronizer->createFolder(trashFolder, "folder", QByteArrayList() << "trash");
        auto trashFolderLocalId = synchronizer->syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, remoteId);
    }
    synchronizer->commit();
}


MaildirResourceFactory::MaildirResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent)
{

}

Sink::Resource *MaildirResourceFactory::createResource(const ResourceContext &context)
{
    return new MaildirResource(context);
}

void MaildirResourceFactory::registerFacades(const QByteArray &name, Sink::FacadeFactory &factory)
{
    factory.registerFacade<Sink::ApplicationDomain::Mail, MaildirResourceMailFacade>(name);
    factory.registerFacade<Sink::ApplicationDomain::Folder, MaildirResourceFolderFacade>(name);
}

void MaildirResourceFactory::registerAdaptorFactories(const QByteArray &name, Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<Sink::ApplicationDomain::Mail, MaildirMailAdaptorFactory>(name);
    registry.registerFactory<Sink::ApplicationDomain::Folder, MaildirFolderAdaptorFactory>(name);
}

void MaildirResourceFactory::removeDataFromDisk(const QByteArray &instanceIdentifier)
{
    MaildirResource::removeFromDisk(instanceIdentifier);
}

