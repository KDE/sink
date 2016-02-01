/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 * Copyright (C) 2015 Christian Mollekopf <mollekopf@kolabsys.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pipeline.h"

#include <QByteArray>
#include <QVector>
#include <QUuid>
#include <QDebug>
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"
#include "modifyentity_generated.h"
#include "deleteentity_generated.h"
#include "entitybuffer.h"
#include "log.h"
#include "domain/applicationdomaintype.h"
#include "definitions.h"
#include "bufferutils.h"

namespace Sink
{

class Pipeline::Private
{
public:
    Private(const QString &resourceName)
        : storage(Sink::storageLocation(), resourceName, Storage::ReadWrite),
        revisionChanged(false)
    {
    }

    Storage storage;
    Storage::Transaction transaction;
    QHash<QString, QVector<Preprocessor *> > processors;
    QHash<QString, DomainTypeAdaptorFactoryInterface::Ptr> adaptorFactory;
    bool revisionChanged;
};

Pipeline::Pipeline(const QString &resourceName, QObject *parent)
    : QObject(parent),
      d(new Private(resourceName))
{
}

Pipeline::~Pipeline()
{
    delete d;
}

void Pipeline::setPreprocessors(const QString &entityType, const QVector<Preprocessor *> &processors)
{
    d->processors[entityType] = processors;
}

void Pipeline::setAdaptorFactory(const QString &entityType, DomainTypeAdaptorFactoryInterface::Ptr factory)
{
    d->adaptorFactory.insert(entityType, factory);
}

void Pipeline::startTransaction()
{
    //TODO call for all types
    //But avoid doing it during cleanup
    // for (auto processor : d->processors[bufferType]) {
    //     processor->startBatch();
    // }
    if (d->transaction) {
        return;
    }
    d->transaction = std::move(storage().createTransaction(Storage::ReadWrite));
}

void Pipeline::commit()
{
    //TODO call for all types
    //But avoid doing it during cleanup
    // for (auto processor : d->processors[bufferType]) {
    //     processor->finalize();
    // }
    const auto revision = Storage::maxRevision(d->transaction);
    Trace() << "Committing " << revision;
    if (d->transaction) {
        d->transaction.commit();
    }
    d->transaction = Storage::Transaction();
    if (d->revisionChanged) {
        d->revisionChanged = false;
        emit revisionUpdated(revision);
    }
}

Storage::Transaction &Pipeline::transaction()
{
    return d->transaction;
}

Storage &Pipeline::storage() const
{
    return d->storage;
}

void Pipeline::storeNewRevision(qint64 newRevision, const flatbuffers::FlatBufferBuilder &fbb, const QByteArray &bufferType, const QByteArray &uid)
{
    Storage::mainDatabase(d->transaction, bufferType).write(Storage::assembleKey(uid, newRevision), BufferUtils::extractBuffer(fbb),
        [](const Storage::Error &error) {
            Warning() << "Failed to write entity";
        }
    );
    d->revisionChanged = true;
    Storage::setMaxRevision(d->transaction, newRevision);
    Storage::recordRevision(d->transaction, newRevision, uid, bufferType);
}

KAsync::Job<qint64> Pipeline::newEntity(void const *command, size_t size)
{
    Trace() << "Pipeline: New Entity";

    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command), size);
        if (!Commands::VerifyCreateEntityBuffer(verifyer)) {
            Warning() << "invalid buffer, not a create entity buffer";
            return KAsync::error<qint64>(0);
        }
    }
    auto createEntity = Commands::GetCreateEntity(command);

    const bool replayToSource = createEntity->replayToSource();
    const QByteArray bufferType = QByteArray(reinterpret_cast<char const*>(createEntity->domainType()->Data()), createEntity->domainType()->size());
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(createEntity->delta()->Data()), createEntity->delta()->size());
        if (!VerifyEntityBuffer(verifyer)) {
            Warning() << "invalid buffer, not an entity buffer";
            return KAsync::error<qint64>(0);
        }
    }
    auto entity = GetEntity(createEntity->delta()->Data());
    if (!entity->resource()->size() && !entity->local()->size()) {
        Warning() << "No local and no resource buffer while trying to create entity.";
        return KAsync::error<qint64>(0);
    }

    QByteArray key;
    if (createEntity->entityId()) {
        key = QByteArray(reinterpret_cast<char const*>(createEntity->entityId()->Data()), createEntity->entityId()->size());
        if (Storage::mainDatabase(d->transaction, bufferType).contains(key)) {
            ErrorMsg() << "An entity with this id already exists: " << key;
            return KAsync::error<qint64>(0);
        }
    }

    if (key.isEmpty()) {
        key = QUuid::createUuid().toString().toUtf8();
    }
    Q_ASSERT(!key.isEmpty());
    const qint64 newRevision = Storage::maxRevision(d->transaction) + 1;

    //Add metadata buffer
    flatbuffers::FlatBufferBuilder metadataFbb;
    auto metadataBuilder = MetadataBuilder(metadataFbb);
    metadataBuilder.add_revision(newRevision);
    metadataBuilder.add_operation(Operation_Creation);
    metadataBuilder.add_replayToSource(replayToSource);
    auto metadataBuffer = metadataBuilder.Finish();
    FinishMetadataBuffer(metadataFbb, metadataBuffer);

    flatbuffers::FlatBufferBuilder fbb;
    EntityBuffer::assembleEntityBuffer(fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize(), entity->resource()->Data(), entity->resource()->size(), entity->local()->Data(), entity->local()->size());

    storeNewRevision(newRevision, fbb, bufferType, key);

    auto adaptorFactory = d->adaptorFactory.value(bufferType);
    if (!adaptorFactory) {
        Warning() << "no adaptor factory for type " << bufferType;
        return KAsync::error<qint64>(0);
    }

    Log() << "Pipeline: wrote entity: " << key << newRevision << bufferType;
    Storage::mainDatabase(d->transaction, bufferType).scan(Storage::assembleKey(key, newRevision), [this, bufferType, newRevision, adaptorFactory, key](const QByteArray &, const QByteArray &value) -> bool {
        auto entity = GetEntity(value);
        auto adaptor = adaptorFactory->createAdaptor(*entity);
        for (auto processor : d->processors[bufferType]) {
            processor->newEntity(key, newRevision, *adaptor, d->transaction);
        }
        return false;
    }, [this](const Storage::Error &error) {
        ErrorMsg() << "Failed to find value in pipeline: " << error.message;
    });
    return KAsync::start<qint64>([newRevision](){
        return newRevision;
    });
}

KAsync::Job<qint64> Pipeline::modifiedEntity(void const *command, size_t size)
{
    Trace() << "Pipeline: Modified Entity";

    const qint64 newRevision = Storage::maxRevision(d->transaction) + 1;

    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command), size);
        if (!Commands::VerifyModifyEntityBuffer(verifyer)) {
            Warning() << "invalid buffer, not a modify entity buffer";
            return KAsync::error<qint64>(0);
        }
    }
    auto modifyEntity = Commands::GetModifyEntity(command);
    Q_ASSERT(modifyEntity);

    const qint64 baseRevision = modifyEntity->revision();
    const bool replayToSource = modifyEntity->replayToSource();
    //TODO rename modifyEntity->domainType to bufferType
    const QByteArray bufferType = QByteArray(reinterpret_cast<char const*>(modifyEntity->domainType()->Data()), modifyEntity->domainType()->size());
    const QByteArray key = QByteArray(reinterpret_cast<char const*>(modifyEntity->entityId()->Data()), modifyEntity->entityId()->size());
    if (bufferType.isEmpty() || key.isEmpty()) {
        Warning() << "entity type or key " << bufferType << key;
        return KAsync::error<qint64>(0);
    }
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(modifyEntity->delta()->Data()), modifyEntity->delta()->size());
        if (!VerifyEntityBuffer(verifyer)) {
            Warning() << "invalid buffer, not an entity buffer";
            return KAsync::error<qint64>(0);
        }
    }

    //TODO use only readPropertyMapper and writePropertyMapper
    auto adaptorFactory = d->adaptorFactory.value(bufferType);
    if (!adaptorFactory) {
        Warning() << "no adaptor factory for type " << bufferType;
        return KAsync::error<qint64>(0);
    }

    auto diffEntity = GetEntity(modifyEntity->delta()->Data());
    Q_ASSERT(diffEntity);
    auto diff = adaptorFactory->createAdaptor(*diffEntity);

    QSharedPointer<ApplicationDomain::BufferAdaptor> current;
    Storage::mainDatabase(d->transaction, bufferType).findLatest(key, [&current, adaptorFactory](const QByteArray &key, const QByteArray &data) -> bool {
        EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
        if (!buffer.isValid()) {
            Warning() << "Read invalid buffer from disk";
        } else {
            current = adaptorFactory->createAdaptor(buffer.entity());
        }
        return false;
    },
    [baseRevision](const Storage::Error &error) {
        Warning() << "Failed to read old revision from storage: " << error.message << "Revision: " << baseRevision;
    });

    if (!current) {
        Warning() << "Failed to read local value " << key;
        return KAsync::error<qint64>(0);
    }

    //resource and uid don't matter at this point
    const ApplicationDomain::ApplicationDomainType existingObject("", "", newRevision, current);
    auto newObject = ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<ApplicationDomain::ApplicationDomainType>(existingObject);

    //Apply diff
    //FIXME only apply the properties that are available in the buffer
    Trace() << "Applying changed properties: " << diff->availableProperties();
    QSet<QByteArray> changeset;
    for (const auto &property : diff->availableProperties()) {
        changeset << property;
        const auto value = diff->getProperty(property);
        if (value.isValid()) {
            newObject->setProperty(property, value);
        }
    }
    //Altough we only set some properties, we want all to be serialized
    newObject->setChangedProperties(changeset);

    //Remove deletions
    if (modifyEntity->deletions()) {
        for (const auto &property : *modifyEntity->deletions()) {
            newObject->setProperty(BufferUtils::extractBuffer(property), QVariant());
        }
    }

    //Add metadata buffer
    flatbuffers::FlatBufferBuilder metadataFbb;
    auto metadataBuilder = MetadataBuilder(metadataFbb);
    metadataBuilder.add_revision(newRevision);
    metadataBuilder.add_operation(Operation_Modification);
    metadataBuilder.add_replayToSource(replayToSource);
    auto metadataBuffer = metadataBuilder.Finish();
    FinishMetadataBuffer(metadataFbb, metadataBuffer);

    flatbuffers::FlatBufferBuilder fbb;
    adaptorFactory->createBuffer(*newObject, fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize());

    storeNewRevision(newRevision, fbb, bufferType, key);
    Log() << "Pipeline: modified entity: " << key << newRevision << bufferType;
    Storage::mainDatabase(d->transaction, bufferType).scan(Storage::assembleKey(key, newRevision), [this, bufferType, newRevision, adaptorFactory, current, key](const QByteArray &k, const QByteArray &value) -> bool {
        if (value.isEmpty()) {
            ErrorMsg() << "Read buffer is empty.";
        }
        auto entity = GetEntity(value.data());
        auto newEntity = adaptorFactory->createAdaptor(*entity);
        for (auto processor : d->processors[bufferType]) {
            processor->modifiedEntity(key, newRevision, *current, *newEntity, d->transaction);
        }
        return false;
    }, [this](const Storage::Error &error) {
        ErrorMsg() << "Failed to find value in pipeline: " << error.message;
    });
    return KAsync::start<qint64>([newRevision](){
        return newRevision;
    });
}

KAsync::Job<qint64> Pipeline::deletedEntity(void const *command, size_t size)
{
    Trace() << "Pipeline: Deleted Entity";

    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command), size);
        if (!Commands::VerifyDeleteEntityBuffer(verifyer)) {
            Warning() << "invalid buffer, not a delete entity buffer";
            return KAsync::error<qint64>(0);
        }
    }
    auto deleteEntity = Commands::GetDeleteEntity(command);

    const bool replayToSource = deleteEntity->replayToSource();
    const QByteArray bufferType = QByteArray(reinterpret_cast<char const*>(deleteEntity->domainType()->Data()), deleteEntity->domainType()->size());
    const QByteArray key = QByteArray(reinterpret_cast<char const*>(deleteEntity->entityId()->Data()), deleteEntity->entityId()->size());

    bool found = false;
    bool alreadyRemoved = false;
    Storage::mainDatabase(d->transaction, bufferType).findLatest(key, [&found, &alreadyRemoved](const QByteArray &key, const QByteArray &data) -> bool {
        auto entity = GetEntity(data.data());
        if (entity && entity->metadata()) {
            auto metadata = GetMetadata(entity->metadata()->Data());
            found = true;
            if (metadata->operation() == Operation_Removal) {
                alreadyRemoved = true;
            }

        }
        return false;
    },
    [](const Storage::Error &error) {
        Warning() << "Failed to read old revision from storage: " << error.message;
    });

    if (!found) {
        Warning() << "Failed to find entity " << key;
        return KAsync::error<qint64>(0);
    }
    if (alreadyRemoved) {
        Warning() << "Entity is already removed " << key;
        return KAsync::error<qint64>(0);
    }

    const qint64 newRevision = Storage::maxRevision(d->transaction) + 1;

    //Add metadata buffer
    flatbuffers::FlatBufferBuilder metadataFbb;
    auto metadataBuilder = MetadataBuilder(metadataFbb);
    metadataBuilder.add_revision(newRevision);
    metadataBuilder.add_operation(Operation_Removal);
    metadataBuilder.add_replayToSource(replayToSource);
    auto metadataBuffer = metadataBuilder.Finish();
    FinishMetadataBuffer(metadataFbb, metadataBuffer);

    flatbuffers::FlatBufferBuilder fbb;
    EntityBuffer::assembleEntityBuffer(fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize(), 0, 0, 0, 0);

    auto adaptorFactory = d->adaptorFactory.value(bufferType);
    if (!adaptorFactory) {
        Warning() << "no adaptor factory for type " << bufferType;
        return KAsync::error<qint64>(0);
    }

    QSharedPointer<ApplicationDomain::BufferAdaptor> current;
    Storage::mainDatabase(d->transaction, bufferType).findLatest(key, [this, bufferType, newRevision, adaptorFactory, key, &current](const QByteArray &, const QByteArray &data) -> bool {
        EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
        if (!buffer.isValid()) {
            Warning() << "Read invalid buffer from disk";
        } else {
            current = adaptorFactory->createAdaptor(buffer.entity());
        }
        return false;
    }, [this](const Storage::Error &error) {
        ErrorMsg() << "Failed to find value in pipeline: " << error.message;
    });

    storeNewRevision(newRevision, fbb, bufferType, key);
    Log() << "Pipeline: deleted entity: "<< newRevision;

    for (auto processor : d->processors[bufferType]) {
        processor->deletedEntity(key, newRevision, *current, d->transaction);
    }

    return KAsync::start<qint64>([newRevision](){
        return newRevision;
    });
}

void Pipeline::cleanupRevision(qint64 revision)
{
    const auto uid = Storage::getUidFromRevision(d->transaction, revision);
    const auto bufferType = Storage::getTypeFromRevision(d->transaction, revision);
    Trace() << "Cleaning up revision " << revision << uid << bufferType;
    Storage::mainDatabase(d->transaction, bufferType).scan(uid, [&](const QByteArray &key, const QByteArray &data) -> bool {
        EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
        if (!buffer.isValid()) {
            Warning() << "Read invalid buffer from disk";
        } else {
            const auto metadata =  flatbuffers::GetRoot<Metadata>(buffer.metadataBuffer());
            const qint64 rev = metadata->revision();
            //Remove old revisions, and the current if the entity has already been removed
            if (rev < revision || metadata->operation() == Operation_Removal) {
                Storage::removeRevision(d->transaction, rev);
                Storage::mainDatabase(d->transaction, bufferType).remove(key);
            }
        }

        return true;
    }, [](const Storage::Error &error) {
        Warning() << "Error while reading: " << error.message;
    }, true);
    Storage::setCleanedUpRevision(d->transaction, revision);
}

qint64 Pipeline::cleanedUpRevision()
{
    return Storage::cleanedUpRevision(d->transaction);
}

Preprocessor::Preprocessor()
    : d(0)
{
}

Preprocessor::~Preprocessor()
{
}

void Preprocessor::startBatch()
{
}

void Preprocessor::finalize()
{
}

} // namespace Sink

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "moc_pipeline.cpp"
#pragma clang diagnostic pop
