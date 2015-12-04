/*
 *   Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
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

#include "resourcefactory.h"
#include "facade.h"
#include "entitybuffer.h"
#include "pipeline.h"
#include "dummycalendar_generated.h"
#include "mail_generated.h"
#include "queuedcommand_generated.h"
#include "createentity_generated.h"
#include "domainadaptor.h"
#include "commands.h"
#include "index.h"
#include "log.h"
#include "domain/event.h"
#include "domain/mail.h"
#include "dummystore.h"
#include "definitions.h"
#include "facadefactory.h"
#include <QDate>
#include <QUuid>

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_EVENT "event"
#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

/**
 * Index types:
 * * uid - property
 * 
 * * Property can be:
 *     * fixed value like uid
 *     * fixed value where we want to do smaller/greater-than comparisons. (like start date)
 *     * range indexes like what date range an event affects.
 *     * group indexes like tree hierarchies as nested sets
 */
class IndexUpdater : public Akonadi2::Preprocessor {
public:
    IndexUpdater(const QByteArray &index, const QByteArray &type, const QByteArray &property)
        :mIndexIdentifier(index),
        mBufferType(type),
        mProperty(property)
    {

    }

    void newEntity(const QByteArray &uid, qint64 revision, const Akonadi2::ApplicationDomain::BufferAdaptor &newEntity, Akonadi2::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        add(newEntity.getProperty(mProperty), uid, transaction);
    }

    void modifiedEntity(const QByteArray &uid, qint64 revision, const Akonadi2::ApplicationDomain::BufferAdaptor &oldEntity, const Akonadi2::ApplicationDomain::BufferAdaptor &newEntity, Akonadi2::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        remove(oldEntity.getProperty(mProperty), uid, transaction);
        add(newEntity.getProperty(mProperty), uid, transaction);
    }

    void deletedEntity(const QByteArray &uid, qint64 revision, const Akonadi2::ApplicationDomain::BufferAdaptor &oldEntity, Akonadi2::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        remove(oldEntity.getProperty(mProperty), uid, transaction);
    }

private:
    void add(const QVariant &value, const QByteArray &uid, Akonadi2::Storage::Transaction &transaction)
    {
        if (value.isValid()) {
            Index(mIndexIdentifier, transaction).add(value.toByteArray(), uid);
        }
    }

    void remove(const QVariant &value, const QByteArray &uid, Akonadi2::Storage::Transaction &transaction)
    {
        //TODO hide notfound error
        Index(mIndexIdentifier, transaction).remove(value.toByteArray(), uid);
    }

    QByteArray mIndexIdentifier;
    QByteArray mBufferType;
    QByteArray mProperty;
};

template<typename DomainType>
class DefaultIndexUpdater : public Akonadi2::Preprocessor {
public:
    void newEntity(const QByteArray &uid, qint64 revision, const Akonadi2::ApplicationDomain::BufferAdaptor &newEntity, Akonadi2::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::index(uid, newEntity, transaction);
    }

    void modifiedEntity(const QByteArray &uid, qint64 revision, const Akonadi2::ApplicationDomain::BufferAdaptor &oldEntity, const Akonadi2::ApplicationDomain::BufferAdaptor &newEntity, Akonadi2::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::removeIndex(uid, oldEntity, transaction);
        Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::index(uid, newEntity, transaction);
    }

    void deletedEntity(const QByteArray &uid, qint64 revision, const Akonadi2::ApplicationDomain::BufferAdaptor &oldEntity, Akonadi2::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::removeIndex(uid, oldEntity, transaction);
    }
};

DummyResource::DummyResource(const QByteArray &instanceIdentifier, const QSharedPointer<Akonadi2::Pipeline> &pipeline)
    : Akonadi2::GenericResource(instanceIdentifier, pipeline)
{
    {
        QVector<Akonadi2::Preprocessor*> eventPreprocessors;
        eventPreprocessors << new DefaultIndexUpdater<Akonadi2::ApplicationDomain::Event>;
        mPipeline->setPreprocessors(ENTITY_TYPE_EVENT, eventPreprocessors);
        mPipeline->setAdaptorFactory(ENTITY_TYPE_EVENT, QSharedPointer<DummyEventAdaptorFactory>::create());
    }
    {
        QVector<Akonadi2::Preprocessor*> mailPreprocessors;
        mailPreprocessors << new DefaultIndexUpdater<Akonadi2::ApplicationDomain::Mail>;
        mPipeline->setPreprocessors(ENTITY_TYPE_MAIL, mailPreprocessors);
        mPipeline->setAdaptorFactory(ENTITY_TYPE_MAIL, QSharedPointer<DummyMailAdaptorFactory>::create());
    }
    {
        QVector<Akonadi2::Preprocessor*> folderPreprocessors;
        folderPreprocessors << new DefaultIndexUpdater<Akonadi2::ApplicationDomain::Folder>;
        mPipeline->setPreprocessors(ENTITY_TYPE_FOLDER, folderPreprocessors);
        mPipeline->setAdaptorFactory(ENTITY_TYPE_FOLDER, QSharedPointer<DummyFolderAdaptorFactory>::create());
    }
}

void DummyResource::createEvent(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &transaction)
{
    //Map the source format to the buffer format (which happens to be an exact copy here)
    auto summary = m_fbb.CreateString(data.value("summary").toString().toStdString());
    auto rid = m_fbb.CreateString(std::string(ridBuffer.constData(), ridBuffer.size()));
    auto description = m_fbb.CreateString(std::string(ridBuffer.constData(), ridBuffer.size()));
    static uint8_t rawData[100];
    auto attachment = Akonadi2::EntityBuffer::appendAsVector(m_fbb, rawData, 100);

    auto builder = DummyCalendar::DummyEventBuilder(m_fbb);
    builder.add_summary(summary);
    builder.add_remoteId(rid);
    builder.add_description(description);
    builder.add_attachment(attachment);
    auto buffer = builder.Finish();
    DummyCalendar::FinishDummyEventBuffer(m_fbb, buffer);
    Akonadi2::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, m_fbb.GetBufferPointer(), m_fbb.GetSize(), 0, 0);
}

QString DummyResource::resolveRemoteId(const QByteArray &bufferType, const QString &remoteId, Akonadi2::Storage::Transaction &transaction)
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


void DummyResource::createMail(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &transaction)
{
    //Map the source format to the buffer format (which happens to be an almost exact copy here)
    auto subject = m_fbb.CreateString(data.value("subject").toString().toStdString());
    auto sender = m_fbb.CreateString(data.value("senderEmail").toString().toStdString());
    auto senderName = m_fbb.CreateString(data.value("senderName").toString().toStdString());
    auto date = m_fbb.CreateString(data.value("date").toDateTime().toString().toStdString());
    auto folder = m_fbb.CreateString(resolveRemoteId(ENTITY_TYPE_MAIL, data.value("parentFolder").toString(), transaction).toStdString());

    auto builder = Akonadi2::ApplicationDomain::Buffer::MailBuilder(m_fbb);
    builder.add_subject(subject);
    builder.add_sender(sender);
    builder.add_senderName(senderName);
    builder.add_unread(data.value("unread").toBool());
    builder.add_important(data.value("important").toBool());
    builder.add_date(date);
    builder.add_folder(folder);
    auto buffer = builder.Finish();
    Akonadi2::ApplicationDomain::Buffer::FinishMailBuffer(m_fbb, buffer);
    Akonadi2::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, 0, 0, m_fbb.GetBufferPointer(), m_fbb.GetSize());
}

void DummyResource::createFolder(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &transaction)
{
    //Map the source format to the buffer format (which happens to be an exact copy here)
    auto name = m_fbb.CreateString(data.value("name").toString().toStdString());
    auto icon = m_fbb.CreateString(data.value("icon").toString().toStdString());
    flatbuffers::Offset<flatbuffers::String> parent;
    bool hasParent = false;
    if (!data.value("parent").toString().isEmpty()) {
        hasParent = true;
        auto akonadiId = resolveRemoteId(ENTITY_TYPE_FOLDER, data.value("parent").toString(), transaction);
        parent = m_fbb.CreateString(akonadiId.toStdString());
    }

    auto builder = Akonadi2::ApplicationDomain::Buffer::FolderBuilder(m_fbb);
    builder.add_name(name);
    if (hasParent) {
        builder.add_parent(parent);
    }
    builder.add_icon(icon);
    auto buffer = builder.Finish();
    Akonadi2::ApplicationDomain::Buffer::FinishFolderBuffer(m_fbb, buffer);
    Akonadi2::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, 0, 0, m_fbb.GetBufferPointer(), m_fbb.GetSize());
}

void DummyResource::synchronize(const QString &bufferType, const QMap<QString, QMap<QString, QVariant> > &data, Akonadi2::Storage::Transaction &transaction, std::function<void(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &)> createEntity)
{
    Akonadi2::Storage store(Akonadi2::storageLocation(), mResourceInstanceIdentifier + ".synchronization", Akonadi2::Storage::ReadWrite);
    auto synchronizationTransaction = store.createTransaction(Akonadi2::Storage::ReadWrite);
    Index ridMapping("rid.mapping", synchronizationTransaction);
    for (auto it = data.constBegin(); it != data.constEnd(); it++) {
        const auto remoteId = it.key().toUtf8();
        auto akonadiId = resolveRemoteId(bufferType.toUtf8(), remoteId, synchronizationTransaction);

        bool found = false;
        transaction.openDatabase(bufferType.toUtf8() + ".main").scan(akonadiId.toUtf8(), [&found](const QByteArray &, const QByteArray &) -> bool {
            found = true;
            return false;
        }, [this](const Akonadi2::Storage::Error &error) {
        }, true);

        if (!found) { //A new entity
            m_fbb.Clear();

            flatbuffers::FlatBufferBuilder entityFbb;
            createEntity(remoteId, it.value(), entityFbb, synchronizationTransaction);

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

KAsync::Job<void> DummyResource::synchronizeWithSource()
{
    Log() << " Synchronizing";
    return KAsync::start<void>([this](KAsync::Future<void> &f) {
        auto transaction = Akonadi2::Storage(Akonadi2::storageLocation(), mResourceInstanceIdentifier, Akonadi2::Storage::ReadOnly).createTransaction(Akonadi2::Storage::ReadOnly);

        synchronize(ENTITY_TYPE_EVENT, DummyStore::instance().events(), transaction, [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &synchronizationTransaction) {
            createEvent(ridBuffer, data, entityFbb, synchronizationTransaction);
        });
        synchronize(ENTITY_TYPE_MAIL, DummyStore::instance().mails(), transaction, [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &synchronizationTransaction) {
            createMail(ridBuffer, data, entityFbb, synchronizationTransaction);
        });
        synchronize(ENTITY_TYPE_FOLDER, DummyStore::instance().folders(), transaction, [this](const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &synchronizationTransaction) {
            createFolder(ridBuffer, data, entityFbb, synchronizationTransaction);
        });

        f.setFinished();
    });
}

void DummyResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    GenericResource::removeFromDisk(instanceIdentifier);
    Akonadi2::Storage(Akonadi2::storageLocation(), instanceIdentifier + ".synchronization", Akonadi2::Storage::ReadWrite).removeFromDisk();
}

DummyResourceFactory::DummyResourceFactory(QObject *parent)
    : Akonadi2::ResourceFactory(parent)
{

}

Akonadi2::Resource *DummyResourceFactory::createResource(const QByteArray &instanceIdentifier)
{
    return new DummyResource(instanceIdentifier);
}

void DummyResourceFactory::registerFacades(Akonadi2::FacadeFactory &factory)
{
    factory.registerFacade<Akonadi2::ApplicationDomain::Event, DummyResourceFacade>(PLUGIN_NAME);
    factory.registerFacade<Akonadi2::ApplicationDomain::Mail, DummyResourceMailFacade>(PLUGIN_NAME);
    factory.registerFacade<Akonadi2::ApplicationDomain::Folder, DummyResourceFolderFacade>(PLUGIN_NAME);
}

