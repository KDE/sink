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


ImapResource::ImapResource(const QByteArray &instanceIdentifier, const QSharedPointer<Sink::Pipeline> &pipeline)
    : Sink::GenericResource(instanceIdentifier, pipeline),
    mMailAdaptorFactory(QSharedPointer<ImapMailAdaptorFactory>::create()),
    mFolderAdaptorFactory(QSharedPointer<ImapFolderAdaptorFactory>::create())
{
    auto config = ResourceConfig::getConfiguration(instanceIdentifier);
    mServer = config.value("server").toString();
    mPort = config.value("port").toInt();

    // auto folderUpdater = new FolderUpdater(QByteArray());
    addType(ENTITY_TYPE_MAIL, mMailAdaptorFactory,
            QVector<Sink::Preprocessor*>() << new DefaultIndexUpdater<Sink::ApplicationDomain::Mail>);
    addType(ENTITY_TYPE_FOLDER, mFolderAdaptorFactory,
            QVector<Sink::Preprocessor*>() << new DefaultIndexUpdater<Sink::ApplicationDomain::Folder>);
}

QByteArray ImapResource::createFolder(const QString &folderPath, const QByteArray &icon, Sink::Storage::Transaction &transaction, Sink::Storage::Transaction &synchronizationTransaction)
{
    auto remoteId = folderPath.toUtf8();
    auto bufferType = ENTITY_TYPE_FOLDER;
    Sink::ApplicationDomain::Folder folder;
    folder.setProperty("name", folderPath.split('/').last());
    folder.setProperty("icon", icon);

    // if (!md.isRoot()) {
    //     folder.setProperty("parent", resolveRemoteId(ENTITY_TYPE_FOLDER, md.parent().path().toUtf8(), synchronizationTransaction));
    // }
    createOrModify(transaction, synchronizationTransaction, *mFolderAdaptorFactory, bufferType, remoteId, folder);
    return remoteId;
}

void ImapResource::synchronizeFolders(const QStringList &folderList, Sink::Storage::Transaction &transaction, Sink::Storage::Transaction &synchronizationTransaction)
{
    const QByteArray bufferType = ENTITY_TYPE_FOLDER;
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
        createFolder(folderPath, "folder", transaction, synchronizationTransaction);
    }
}

void ImapResource::synchronizeMails(Sink::Storage::Transaction &transaction, Sink::Storage::Transaction &synchronizationTransaction, const QString &path, const QVector<Message> &messages)
{
    auto time = QSharedPointer<QTime>::create();
    time->start();
    const QByteArray bufferType = ENTITY_TYPE_MAIL;


    Trace() << "Importing new mail.";

    // Trace() << "Looking into " << listingPath;

    const auto folderLocalId = resolveRemoteId(ENTITY_TYPE_FOLDER, path.toUtf8(), synchronizationTransaction);

    //This is not a full listing
    // auto property = "folder";
    // scanForRemovals(transaction, synchronizationTransaction, bufferType,
    //     [&](const std::function<void(const QByteArray &)> &callback) {
    //         Index index(bufferType + ".index." + property, transaction);
    //         index.lookup(folderLocalId, [&](const QByteArray &sinkId) {
    //             callback(sinkId);
    //         },
    //         [&](const Index::Error &error) {
    //             Warning() << "Error in index: " <<  error.message << property;
    //         });
    //     },
    //     [](const QByteArray &remoteId) -> bool {
    //         return QFile(remoteId).exists();
    //     }
    // );

    mSynchronizerQueue.startTransaction();
    int count = 0;
    for (const auto &message : messages) {
        count++;
        const auto remoteId = path.toUtf8() + "/" + QByteArray::number(message.uid);

        Trace() << "Found a mail " << remoteId << message.msg->subject(true)->asUnicodeString() << message.flags;

        Sink::ApplicationDomain::Mail mail;
        mail.setFolder(folderLocalId);
        //FIXME this should come from the mime message, extracted in the pipeline
        mail.setExtractedSubject(message.msg->subject(true)->asUnicodeString());

        auto filePath = Sink::resourceStorageLocation(mResourceInstanceIdentifier) + "/" + remoteId;
        QDir().mkpath(Sink::resourceStorageLocation(mResourceInstanceIdentifier) + "/" + path.toUtf8());
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            Warning() << "Failed to open file for writing: " << file.errorString();
        }
        const auto content = message.msg->encodedContent();
        file.write(content);
        mail.setMimeMessagePath(filePath);
        //FIXME  Not sure if these are the actual flags
        mail.setUnread(message.flags.contains("\\SEEN"));
        mail.setImportant(message.flags.contains("\\FLAGGED"));

        createOrModify(transaction, synchronizationTransaction, *mMailAdaptorFactory, bufferType, remoteId, mail);
    }
    mSynchronizerQueue.commit();
    const auto elapsed = time->elapsed();
    Log() << "Synchronized " << count << " mails in " << path << Sink::Log::TraceTime(elapsed) << " " << elapsed/qMax(count, 1) << " [ms/mail]";
}

KAsync::Job<void> ImapResource::synchronizeWithSource(Sink::Storage &mainStore, Sink::Storage &synchronizationStore)
{
    Log() << " Synchronizing";
    return KAsync::start<void>([this, &mainStore, &synchronizationStore](KAsync::Future<void> future) {
        ImapServerProxy imap(mServer, mPort);
        QStringList folderList;
        // QList<KAsync::Future<void>> waitCondition;
        auto folderFuture = imap.fetchFolders([this, &imap, &mainStore, &synchronizationStore, &folderList](const QStringList &folders) {
            auto transaction = mainStore.createTransaction(Sink::Storage::ReadOnly);
            auto syncTransaction = synchronizationStore.createTransaction(Sink::Storage::ReadWrite);
            synchronizeFolders(folders, transaction, syncTransaction);
            transaction.commit();
            syncTransaction.commit();
            folderList << folders;

        });
        folderFuture.waitForFinished();
        if (folderFuture.errorCode()) {
            future.setError(1, "Folder list sync failed");
            return;
        }

        for (const auto &folder : folderList) {
            // auto transaction = mainStore.createTransaction(Sink::Storage::ReadOnly);
            // auto syncTransaction = synchronizationStore.createTransaction(Sink::Storage::ReadOnly);

            //TODO load entity to read sync settings should we have some (if the folder is existing already)
            //Note that this will not work if we change any of those settings in the pipeline
            //
            // auto mainDatabase = Sink::Storage::mainDatabase(transaction, ENTITY_TYPE_FOLDER);
            // const auto sinkId = resolveRemoteId(ENTITY_TYPE_FOLDER, folder.toUtf8(), syncTransaction);
            // const auto found = mainDatabase.contains(sinkId);
            // if (found) {
            //     if (auto current = getLatest(mainDatabase, sinkId, mFolderAdaptorFactory)) {
            //
            //     }
            // }

            // transaction.commit();
            // syncTransaction.commit();

            auto messagesFuture = imap.fetchMessages(folder, [this, &mainStore, &synchronizationStore, folder](const QVector<Message> &messages) {
                auto transaction = mainStore.createTransaction(Sink::Storage::ReadOnly);
                auto syncTransaction = synchronizationStore.createTransaction(Sink::Storage::ReadWrite);
                Trace() << "Synchronizing mails" << folder;
                synchronizeMails(transaction, syncTransaction, folder, messages);
                transaction.commit();
                syncTransaction.commit();
            });
            messagesFuture.waitForFinished();
            if (messagesFuture.errorCode()) {
                future.setError(1, "Folder sync failed: " + folder);
                return;
            }
        }


        // auto transaction = mainStore.createTransaction(Sink::Storage::ReadWrite);
        // auto mainDatabase = Sink::Storage::mainDatabase(transaction, ENTITY_TYPE_FOLDER);
        // mainDatabase.scan("", [&](const QByteArray &key, const QByteArray &data) {
        //     return true;
        // });
        //TODO now fetch all folders and iterate over them and synchronize each one

        Log() << "Done Synchronizing";
        future.setFinished();
    });
}

KAsync::Job<void> ImapResource::replay(Sink::Storage &synchronizationStore, const QByteArray &type, const QByteArray &key, const QByteArray &value)
{
    //TODO implement
    return KAsync::null<void>();
}

void ImapResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    GenericResource::removeFromDisk(instanceIdentifier);
    Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".synchronization", Sink::Storage::ReadWrite).removeFromDisk();
}

KAsync::Job<void> ImapResource::inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
{
    //TODO
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

