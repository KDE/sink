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

#include "davresource.h"

#include "facade.h"
#include "resourceconfig.h"
#include "index.h"
#include "log.h"
#include "definitions.h"
#include "inspection.h"
#include "synchronizer.h"
#include "inspector.h"

#include "facadefactory.h"
#include "adaptorfactoryregistry.h"

#include "contactpreprocessor.h"

#include <KDAV/DavCollection>
#include <KDAV/DavCollectionsFetchJob>
#include <KDAV/DavItem>
#include <KDAV/DavItemsListJob>
#include <KDAV/DavItemFetchJob>
#include <KDAV/EtagCache>

#include <QDir>
#include <QDirIterator>

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_CONTACT "contact"
#define ENTITY_TYPE_ADDRESSBOOK "folder"

SINK_DEBUG_AREA("davresource")

using namespace Sink;

/*static QString getFilePathFromMimeMessagePath(const QString &mimeMessagePath)
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
            KPIM::Contactdir maildir(path, false);
            if (!maildir.isValid(true)) {
                SinkWarning() << "Maildir is not existing: " << path;
            }
            auto identifier = maildir.addEntryFromPath(oldPath);
            return path + "/" + identifier;
        } else {
            //Handle moves
            const auto path = getPath(folder);
            KPIM::Contactdir maildir(path, false);
            if (!maildir.isValid(true)) {
                SinkWarning() << "Maildir is not existing: " << path;
            }
            auto oldIdentifier = KPIM::Contactdir::getKeyFromFile(oldPath);
            auto pathParts = oldPath.split('/');
            pathParts.takeLast();
            auto oldDirectory = pathParts.join('/');
            if (oldDirectory == path) {
                return oldPath;
            }
            KPIM::Contactdir oldMaildir(oldDirectory, false);
            if (!oldMaildir.isValid(false)) {
                SinkWarning() << "Maildir is not existing: " << path;
            }
            auto identifier = oldMaildir.moveEntryTo(oldIdentifier, maildir);
            return path + "/" + identifier;
        }
    }

    void newEntity(Sink::ApplicationDomain::ApplicationDomainType &newEntity) Q_DECL_OVERRIDE
    {
        auto mail = newEntity.cast<ApplicationDomain::Contact>();
        const auto mimeMessage = mail.getMimeMessagePath();
        if (!mimeMessage.isNull()) {
            const auto path = moveMessage(mimeMessage, mail.getFolder());
            auto blob = ApplicationDomain::BLOB{path};
            blob.isExternal = false;
            mail.setProperty(ApplicationDomain::Contact::MimeMessage::name, QVariant::fromValue(blob));
        }
    }

    void modifiedEntity(const Sink::ApplicationDomain::ApplicationDomainType &oldEntity, Sink::ApplicationDomain::ApplicationDomainType &newEntity) Q_DECL_OVERRIDE
    {
        auto newMail = newEntity.cast<ApplicationDomain::Contact>();
        const ApplicationDomain::Contact oldMail{oldEntity};
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
                newMail.setProperty(ApplicationDomain::Contact::MimeMessage::name, QVariant::fromValue(blob));
                //Remove the olde mime message if there is a new one
                QFile::remove(oldPath);
            }
        }

        auto mimeMessagePath = newMail.getMimeMessagePath();
        const auto maildirPath = getPath(newMail.getFolder());
        KPIM::Contactdir maildir(maildirPath, false);
        const auto file = getFilePathFromMimeMessagePath(mimeMessagePath);
        QString identifier = KPIM::Contactdir::getKeyFromFile(file);

        //get flags from
        KPIM::Contactdir::Flags flags;
        if (!newMail.getUnread()) {
            flags |= KPIM::Contactdir::Seen;
        }
        if (newMail.getImportant()) {
            flags |= KPIM::Contactdir::Flagged;
        }

        maildir.changeEntryFlags(identifier, flags);
    }

    void deletedEntity(const Sink::ApplicationDomain::ApplicationDomainType &oldEntity) Q_DECL_OVERRIDE
    {
        const ApplicationDomain::Contact oldMail{oldEntity};
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
        KPIM::Contactdir maildir(path, false);
        maildir.create();
    }

    void modifiedEntity(const Sink::ApplicationDomain::ApplicationDomainType &oldEntity, Sink::ApplicationDomain::ApplicationDomainType &newEntity) Q_DECL_OVERRIDE
    {
    }

    void deletedEntity(const Sink::ApplicationDomain::ApplicationDomainType &oldEntity) Q_DECL_OVERRIDE
    {
    }
    QString mMaildirPath;
};*/

static KAsync::Job<void> runJob(KJob *job)
{
    return KAsync::start<void>([job](KAsync::Future<void> &future) {
        QObject::connect(job, &KJob::result, [&future](KJob *job) {
            SinkTrace() << "Job done: " << job->metaObject()->className();
            if (job->error()) {
                SinkWarning() << "Job failed: " << job->errorString();
                future.setError(job->error(), job->errorString());
            } else {
                future.setFinished();
            }
        });
        SinkTrace() << "Starting job: " << job->metaObject()->className();
        job->start();
    });
}

class ContactSynchronizer : public Sink::Synchronizer {
public:
    ContactSynchronizer(const Sink::ResourceContext &resourceContext)
        : Sink::Synchronizer(resourceContext)
    {

    }

        QByteArray createAddressbook(const QString &folderName, const QString &folderPath, const QString &parentFolderRid, const QByteArray &icon)
    {
        SinkTrace() << "Creating addressbook: " << folderName << parentFolderRid;
        const auto remoteId = folderPath.toUtf8();
        const auto bufferType = ENTITY_TYPE_ADDRESSBOOK;
        Sink::ApplicationDomain::Folder folder;
        folder.setName(folderName);
        folder.setIcon(icon);
        QHash<QByteArray, Query::Comparator> mergeCriteria;

        if (!parentFolderRid.isEmpty()) {
            folder.setParent(syncStore().resolveRemoteId(ENTITY_TYPE_ADDRESSBOOK, parentFolderRid.toUtf8()));
        }
        createOrModify(bufferType, remoteId, folder, mergeCriteria);
        return remoteId;
    }

    void synchronizeAddressbooks(const KDAV::DavCollection::List &folderList)
    {
        const QByteArray bufferType = ENTITY_TYPE_ADDRESSBOOK;
        SinkTrace() << "Found addressbooks " << folderList.size();

        QVector<QByteArray> ridList;
        for(const auto &f : folderList) {
            const auto &rid = f.url().toDisplayString();
            ridList.append(rid.toUtf8());
            createAddressbook(f.displayName(), rid, "", "addressbook");
        }

        scanForRemovals(bufferType,
            [&ridList](const QByteArray &remoteId) -> bool {
                return ridList.contains(remoteId);
            }
        );
    }

    QList<Synchronizer::SyncRequest> getSyncRequests(const Sink::QueryBase &query) Q_DECL_OVERRIDE
    {
        QList<Synchronizer::SyncRequest> list;
        if (!query.type().isEmpty()) {
            //We want to synchronize something specific
            list << Synchronizer::SyncRequest{query};
        } else {
            //We want to synchronize everything
            list << Synchronizer::SyncRequest{Sink::QueryBase(ApplicationDomain::getTypeName<ApplicationDomain::Contact>())};
        }
        return list;
    }

    KAsync::Job<void> synchronizeWithSource(const Sink::QueryBase &query) Q_DECL_OVERRIDE
    {
        if (query.type() == ApplicationDomain::getTypeName<ApplicationDomain::Folder>()) {
            auto collectionsFetchJob = new KDAV::DavCollectionsFetchJob(mResourceUrl);
            auto job = runJob(collectionsFetchJob).then<void>([this, collectionsFetchJob] {
                synchronizeAddressbooks(collectionsFetchJob ->collections());
            });
            return job;
        } else if (query.type() == ApplicationDomain::getTypeName<ApplicationDomain::Contact>()) {
            auto collectionsFetchJob = new KDAV::DavCollectionsFetchJob(mResourceUrl);
            auto job = runJob(collectionsFetchJob).then<KDAV::DavCollection::List>([this, collectionsFetchJob] {
                synchronizeAddressbooks(collectionsFetchJob ->collections());
                return collectionsFetchJob->collections();
            })
            .serialEach<void>([this](const KDAV::DavCollection &collection) {
                auto collId = collection.url().toDisplayString().toLatin1();
                auto ctag = collection.CTag().toLatin1();
                if (ctag != syncStore().readValue(collId + "_ctag")) {
                    SinkTrace() << "Syncing " << collId;
                    auto cache = std::shared_ptr<KDAV::EtagCache>(new KDAV::EtagCache());
                    auto davItemsListJob = new KDAV::DavItemsListJob(collection.url(), cache);
                    const QByteArray bufferType = ENTITY_TYPE_CONTACT;
                    QHash<QByteArray, Query::Comparator> mergeCriteria;
                    QByteArrayList ridList;
                    auto colljob = runJob(davItemsListJob).then<KDAV::DavItem::List>([davItemsListJob] {
                        return KAsync::value(davItemsListJob->items());
                    })
                    .serialEach<QByteArray>([this, &ridList, bufferType, mergeCriteria] (const KDAV::DavItem &item) {
                        QByteArray rid = item.url().toDisplayString().toUtf8();
                        if (item.etag().toLatin1() != syncStore().readValue(rid + "_etag")){
                            SinkTrace() << "Updating " << rid;
                            auto davItemFetchJob = new KDAV::DavItemFetchJob(item);
                            auto itemjob = runJob(davItemFetchJob)
                            .then<KDAV::DavItem>([this, davItemFetchJob, bufferType, mergeCriteria] {
                                const auto item = davItemFetchJob->item();
                                const auto rid = item.url().toDisplayString().toUtf8();
                                Sink::ApplicationDomain::Contact contact;
                                contact.setVcard(item.data());
                                createOrModify(bufferType, rid, contact, mergeCriteria);
                                return item;
                            })
                            .then<QByteArray>([this, &ridList] (const KDAV::DavItem &item) {
                                const auto rid = item.url().toDisplayString().toUtf8();
                                syncStore().writeValue(rid + "_etag", item.etag().toLatin1());
                                //ridList << rid;
                                return rid;
                            });
                            return itemjob;
                        } else {
                            //ridList << rid;
                            return KAsync::value(rid);
                        }
                    })
                    /*.then<void>([this, ridList, bufferType] () {
                        scanForRemovals(bufferType,
                            [&ridList](const QByteArray &remoteId) -> bool {
                                return ridList.contains(remoteId);
                        });
                    })*/
                    /*.then<void>([this, bufferType] (const QByteArrayList &ridList) {
                        scanForRemovals(bufferType,
                            [&ridList](const QByteArray &remoteId) -> bool {
                                return ridList.contains(remoteId);
                        });
                    })*/
                    .then<void>([this, collId, ctag] () {
                        syncStore().writeValue(collId + "_ctag", ctag);
                    });
                    return colljob;
                } else {
                    return KAsync::null<void>();
                }
            });
            return job;
        } else {
            return KAsync::null<void>();
        }
    }

KAsync::Job<QByteArray> replay(const ApplicationDomain::Contact &contact, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        /*
        if (operation == Sink::Operation_Creation) {
            const auto remoteId = getFilePathFromMimeMessagePath(mail.getMimeMessagePath());
            SinkTrace() << "Contact created: " << remoteId;
            return KAsync::value(remoteId.toUtf8());
        } else if (operation == Sink::Operation_Removal) {
            SinkTrace() << "Removing a contact " << oldRemoteId;
            return KAsync::null<QByteArray>();
        } else if (operation == Sink::Operation_Modification) {
            SinkTrace() << "Modifying a contact: " << oldRemoteId;
            const auto remoteId = getFilePathFromMimeMessagePath(mail.getMimeMessagePath());
            return KAsync::value(remoteId.toUtf8());
        }*/
        return KAsync::null<QByteArray>();
    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Folder &folder, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        /*
        if (operation == Sink::Operation_Creation) {
            auto folderName = folder.getName();
            //FIXME handle non toplevel folders
            auto path = mMaildirPath + "/" + folderName;
            SinkTrace() << "Creating a new folder: " << path;
            KPIM::Contactdir maildir(path, false);
            maildir.create();
            return KAsync::value(path.toUtf8());
        } else if (operation == Sink::Operation_Removal) {
            const auto path = oldRemoteId;
            SinkTrace() << "Removing a folder: " << path;
            KPIM::Contactdir maildir(path, false);
            maildir.remove();
            return KAsync::null<QByteArray>();
        } else if (operation == Sink::Operation_Modification) {
            SinkWarning() << "Folder modifications are not implemented";
            return KAsync::value(oldRemoteId);
        }*/
        return KAsync::null<QByteArray>();
    }

public:
    KDAV::DavUrl mResourceUrl;
};

/*
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

        Sink::Storage::EntityStore entityStore(mResourceContext, {"maildirresource"});
        auto syncStore = QSharedPointer<SynchronizerStore>::create(synchronizationTransaction);

        SinkTrace() << "Inspecting " << inspectionType << domainType << entityId << property << expectedValue;

        if (domainType == ENTITY_TYPE_MAIL) {
            auto mail = entityStore.readLatest<Sink::ApplicationDomain::Contact>(entityId);
            const auto filePath = getFilePathFromMimeMessagePath(mail.getMimeMessagePath());

            if (inspectionType == Sink::ResourceControl::Inspection::PropertyInspectionType) {
                if (property == "unread") {
                    const auto flags = KPIM::Contactdir::readEntryFlags(filePath.split('/').last());
                    if (expectedValue.toBool() && (flags & KPIM::Contactdir::Seen)) {
                        return KAsync::error<void>(1, "Expected unread but couldn't find it.");
                    }
                    if (!expectedValue.toBool() && !(flags & KPIM::Contactdir::Seen)) {
                        return KAsync::error<void>(1, "Expected read but couldn't find it.");
                    }
                    return KAsync::null<void>();
                }
                if (property == "subject") {
                    KMime::Message *msg = new KMime::Message;
                    msg->setHead(KMime::CRLFtoLF(KPIM::Contactdir::readEntryHeadersFromFile(filePath)));
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
};*/


DavResource::DavResource(const Sink::ResourceContext &resourceContext)
    : Sink::GenericResource(resourceContext)
{
    auto config = ResourceConfig::getConfiguration(resourceContext.instanceId());
    auto resourceUrl = QUrl::fromUserInput(config.value("resourceUrl").toString());
    resourceUrl.setUserName(config.value("username").toString());
    resourceUrl.setPassword(config.value("password").toString());

    mResourceUrl = KDAV::DavUrl(resourceUrl, KDAV::CardDav);

    auto synchronizer = QSharedPointer<ContactSynchronizer>::create(resourceContext);
    synchronizer->mResourceUrl = mResourceUrl;
    setupSynchronizer(synchronizer);
    //setupInspector(QSharedPointer<MaildirInspector>::create(resourceContext));

    setupPreprocessors(ENTITY_TYPE_CONTACT, QVector<Sink::Preprocessor*>() << new ContactPropertyExtractor);
}


DavResourceFactory::DavResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent,
            {"-folder.rename"}
            )
{
}

Sink::Resource *DavResourceFactory::createResource(const ResourceContext &context)
{
    return new DavResource(context);
}

void DavResourceFactory::registerFacades(const QByteArray &name, Sink::FacadeFactory &factory)
{
    factory.registerFacade<Sink::ApplicationDomain::Contact, DavResourceContactFacade>(name);
    factory.registerFacade<Sink::ApplicationDomain::Folder, DavResourceFolderFacade>(name);
}

void DavResourceFactory::registerAdaptorFactories(const QByteArray &name, Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<Sink::ApplicationDomain::Contact, ContactAdaptorFactory>(name);
    registry.registerFactory<Sink::ApplicationDomain::Folder, AddressbookAdaptorFactory>(name);
}

void DavResourceFactory::removeDataFromDisk(const QByteArray &instanceIdentifier)
{
    DavResource::removeFromDisk(instanceIdentifier);
}
