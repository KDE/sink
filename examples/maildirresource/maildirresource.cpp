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
    : Akonadi2::GenericResource(instanceIdentifier, pipeline)
{
    addType(ENTITY_TYPE_MAIL, QSharedPointer<MaildirMailAdaptorFactory>::create(),
            QVector<Akonadi2::Preprocessor*>() << new DefaultIndexUpdater<Akonadi2::ApplicationDomain::Mail>);
    addType(ENTITY_TYPE_FOLDER, QSharedPointer<MaildirFolderAdaptorFactory>::create(),
            QVector<Akonadi2::Preprocessor*>() << new DefaultIndexUpdater<Akonadi2::ApplicationDomain::Folder>);
    auto config = ResourceConfig::getConfiguration(instanceIdentifier);
    mMaildirPath = config.value("path").toString();
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

void MaildirResource::synchronizeFolders(Akonadi2::Storage::Transaction &transaction)
{
    const QString bufferType = ENTITY_TYPE_FOLDER;
    QStringList folderList = listAvailableFolders();
    Trace() << "Found folders " << folderList;

    Akonadi2::Storage store(Akonadi2::storageLocation(), mResourceInstanceIdentifier + ".synchronization", Akonadi2::Storage::ReadWrite);
    auto synchronizationTransaction = store.createTransaction(Akonadi2::Storage::ReadWrite);
    for (const auto folder : folderList) {
        const auto remoteId = folder.toUtf8();
        auto akonadiId = resolveRemoteId(bufferType.toUtf8(), remoteId, synchronizationTransaction);

        bool found = false;
        transaction.openDatabase(bufferType.toUtf8() + ".main").scan(akonadiId.toUtf8(), [&found](const QByteArray &, const QByteArray &) -> bool {
            found = true;
            return false;
        }, [this](const Akonadi2::Storage::Error &error) {
        }, true);

        if (!found) { //A new entity
            m_fbb.Clear();

            KPIM::Maildir md(folder, folder == mMaildirPath);

            flatbuffers::FlatBufferBuilder entityFbb;
            auto name = m_fbb.CreateString(md.name().toStdString());
            auto icon = m_fbb.CreateString("folder");
            flatbuffers::Offset<flatbuffers::String> parent;

            if (!md.isRoot()) {
                auto akonadiId = resolveRemoteId(ENTITY_TYPE_FOLDER, md.parent().path(), synchronizationTransaction);
                parent = m_fbb.CreateString(akonadiId.toStdString());
            }

            auto builder = Akonadi2::ApplicationDomain::Buffer::FolderBuilder(m_fbb);
            builder.add_name(name);
            if (!md.isRoot()) {
                builder.add_parent(parent);
            }
            builder.add_icon(icon);
            auto buffer = builder.Finish();
            Akonadi2::ApplicationDomain::Buffer::FinishFolderBuffer(m_fbb, buffer);
            Akonadi2::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, 0, 0, m_fbb.GetBufferPointer(), m_fbb.GetSize());

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

void MaildirResource::synchronizeMails(Akonadi2::Storage::Transaction &transaction, const QString &path)
{
    Trace() << "Synchronizing mails" << path;
    const QString bufferType = ENTITY_TYPE_MAIL;

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
        auto akonadiId = resolveRemoteId(bufferType.toUtf8(), remoteId, synchronizationTransaction);

        bool found = false;
        transaction.openDatabase(bufferType.toUtf8() + ".main").scan(akonadiId.toUtf8(), [&found](const QByteArray &, const QByteArray &) -> bool {
            found = true;
            return false;
        }, [this](const Akonadi2::Storage::Error &error) {
        }, true);

        if (!found) { //A new entity
            m_fbb.Clear();

            KMime::Message *msg = new KMime::Message;
            auto filepath = listingPath + QDir::separator() + fileName;
            msg->setHead(KMime::CRLFtoLF(maildir.readEntryHeadersFromFile(filepath)));
            msg->parse();

            const auto flags = maildir.readEntryFlags(fileName);

            Trace() << "Found a mail " << filePath << fileName << msg->subject(true)->asUnicodeString();
            flatbuffers::FlatBufferBuilder entityFbb;
            auto subject = m_fbb.CreateString(msg->subject(true)->asUnicodeString().toStdString());
            auto sender = m_fbb.CreateString(msg->from(true)->asUnicodeString().toStdString());
            auto senderName = m_fbb.CreateString(msg->from(true)->asUnicodeString().toStdString());
            auto date = m_fbb.CreateString(msg->date(true)->dateTime().toString().toStdString());
            auto folder = m_fbb.CreateString(resolveRemoteId(ENTITY_TYPE_FOLDER, path, synchronizationTransaction).toStdString());
            auto mimeMessage = m_fbb.CreateString(filepath.toStdString());

            auto builder = Akonadi2::ApplicationDomain::Buffer::MailBuilder(m_fbb);
            builder.add_subject(subject);
            builder.add_sender(sender);
            builder.add_senderName(senderName);
            builder.add_unread(!(flags & KPIM::Maildir::Seen));
            builder.add_important(flags & KPIM::Maildir::Flagged);
            builder.add_date(date);
            builder.add_folder(folder);
            builder.add_mimeMessage(mimeMessage);
            auto buffer = builder.Finish();
            Akonadi2::ApplicationDomain::Buffer::FinishMailBuffer(m_fbb, buffer);
            Akonadi2::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, 0, 0, m_fbb.GetBufferPointer(), m_fbb.GetSize());

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

