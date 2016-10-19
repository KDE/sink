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
#include <QTime>
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"
#include "modifyentity_generated.h"
#include "deleteentity_generated.h"
#include "entitybuffer.h"
#include "log.h"
#include "domain/applicationdomaintype.h"
#include "adaptorfactoryregistry.h"
#include "definitions.h"
#include "bufferutils.h"
#include "storage/entitystore.h"

SINK_DEBUG_AREA("pipeline")

using namespace Sink;
using namespace Sink::Storage;

class Pipeline::Private
{
public:
    Private(const ResourceContext &context) : resourceContext(context), entityStore(context), revisionChanged(false)
    {
    }

    ResourceContext resourceContext;
    Storage::EntityStore entityStore;
    QHash<QString, QVector<QSharedPointer<Preprocessor>>> processors;
    bool revisionChanged;
    QTime transactionTime;
    int transactionItemCount;
};


Pipeline::Pipeline(const ResourceContext &context) : QObject(nullptr), d(new Private(context))
{
}

Pipeline::~Pipeline()
{
}

void Pipeline::setPreprocessors(const QString &entityType, const QVector<Preprocessor *> &processors)
{
    auto &list = d->processors[entityType];
    list.clear();
    for (auto p : processors) {
        p->setup(d->resourceContext.resourceType, d->resourceContext.instanceId(), this);
        list.append(QSharedPointer<Preprocessor>(p));
    }
}

void Pipeline::startTransaction()
{
    // TODO call for all types
    // But avoid doing it during cleanup
    // for (auto processor : d->processors[bufferType]) {
    //     processor->startBatch();
    // }
    SinkTrace() << "Starting transaction.";
    d->transactionTime.start();
    d->transactionItemCount = 0;
    d->entityStore.startTransaction(DataStore::ReadWrite);
}

void Pipeline::commit()
{
    // TODO call for all types
    // But avoid doing it during cleanup
    // for (auto processor : d->processors[bufferType]) {
    //     processor->finalize();
    // }
    if (!d->revisionChanged) {
        d->entityStore.abortTransaction();
        return;
    }
    const auto revision = d->entityStore.maxRevision();
    const auto elapsed = d->transactionTime.elapsed();
    SinkLog() << "Committing revision: " << revision << ":" << d->transactionItemCount << " items in: " << Log::TraceTime(elapsed) << " "
            << (double)elapsed / (double)qMax(d->transactionItemCount, 1) << "[ms/item]";
    d->entityStore.commitTransaction();
    if (d->revisionChanged) {
        d->revisionChanged = false;
        emit revisionUpdated(revision);
    }
}

KAsync::Job<qint64> Pipeline::newEntity(void const *command, size_t size)
{
    d->transactionItemCount++;

    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command), size);
        if (!Commands::VerifyCreateEntityBuffer(verifyer)) {
            SinkWarning() << "invalid buffer, not a create entity buffer";
            return KAsync::error<qint64>(0);
        }
    }
    auto createEntity = Commands::GetCreateEntity(command);

    const bool replayToSource = createEntity->replayToSource();
    const QByteArray bufferType = QByteArray(reinterpret_cast<char const *>(createEntity->domainType()->Data()), createEntity->domainType()->size());
    QByteArray key;
    if (createEntity->entityId()) {
        key = QByteArray(reinterpret_cast<char const *>(createEntity->entityId()->Data()), createEntity->entityId()->size());
        if (d->entityStore.contains(bufferType, key)) {
            SinkError() << "An entity with this id already exists: " << key;
            return KAsync::error<qint64>(0);
        }
    }

    if (key.isEmpty()) {
        key = DataStore::generateUid();
    }
    SinkTrace() << "New Entity. Type: " << bufferType << "uid: "<< key << " replayToSource: " << replayToSource;
    Q_ASSERT(!key.isEmpty());

    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(createEntity->delta()->Data()), createEntity->delta()->size());
        if (!VerifyEntityBuffer(verifyer)) {
            SinkWarning() << "invalid buffer, not an entity buffer";
            return KAsync::error<qint64>(0);
        }
    }
    auto entity = GetEntity(createEntity->delta()->Data());
    if (!entity->resource()->size() && !entity->local()->size()) {
        SinkWarning() << "No local and no resource buffer while trying to create entity.";
        return KAsync::error<qint64>(0);
    }

    auto adaptorFactory = Sink::AdaptorFactoryRegistry::instance().getFactory(d->resourceContext.resourceType, bufferType);
    if (!adaptorFactory) {
        SinkWarning() << "no adaptor factory for type " << bufferType;
        return KAsync::error<qint64>(0);
    }

    auto adaptor = adaptorFactory->createAdaptor(*entity);
    auto memoryAdaptor = QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create(*(adaptor), adaptor->availableProperties());

    d->revisionChanged = true;
    auto revision = d->entityStore.maxRevision();
    auto o = Sink::ApplicationDomain::ApplicationDomainType{d->resourceContext.instanceId(), key, revision, memoryAdaptor};
    o.setChangedProperties(o.availableProperties().toSet());

    auto preprocess = [&, this](ApplicationDomain::ApplicationDomainType &newEntity) {
        foreach (const auto &processor, d->processors[bufferType]) {
            processor->newEntity(newEntity);
        }
    };

    if (!d->entityStore.add(bufferType, o, replayToSource, preprocess)) {
        return KAsync::error<qint64>(0);
    }

    return KAsync::value(d->entityStore.maxRevision());
}

KAsync::Job<qint64> Pipeline::modifiedEntity(void const *command, size_t size)
{
    d->transactionItemCount++;

    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command), size);
        if (!Commands::VerifyModifyEntityBuffer(verifyer)) {
            SinkWarning() << "invalid buffer, not a modify entity buffer";
            return KAsync::error<qint64>(0);
        }
    }
    auto modifyEntity = Commands::GetModifyEntity(command);
    Q_ASSERT(modifyEntity);
    QList<QByteArray> changeset;
    if (modifyEntity->modifiedProperties()) {
        changeset = BufferUtils::fromVector(*modifyEntity->modifiedProperties());
    } else {
        SinkWarning() << "No changeset available";
    }
    const qint64 baseRevision = modifyEntity->revision();
    const bool replayToSource = modifyEntity->replayToSource();

    const QByteArray bufferType = QByteArray(reinterpret_cast<char const *>(modifyEntity->domainType()->Data()), modifyEntity->domainType()->size());
    const QByteArray key = QByteArray(reinterpret_cast<char const *>(modifyEntity->entityId()->Data()), modifyEntity->entityId()->size());
    SinkTrace() << "Modified Entity. Type: " << bufferType << "uid: "<< key << " replayToSource: " << replayToSource;
    if (bufferType.isEmpty() || key.isEmpty()) {
        SinkWarning() << "entity type or key " << bufferType << key;
        return KAsync::error<qint64>(0);
    }
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(modifyEntity->delta()->Data()), modifyEntity->delta()->size());
        if (!VerifyEntityBuffer(verifyer)) {
            SinkWarning() << "invalid buffer, not an entity buffer";
            return KAsync::error<qint64>(0);
        }
    }

    auto adaptorFactory = Sink::AdaptorFactoryRegistry::instance().getFactory(d->resourceContext.resourceType, bufferType);
    if (!adaptorFactory) {
        SinkWarning() << "no adaptor factory for type " << bufferType;
        return KAsync::error<qint64>(0);
    }

    auto diffEntity = GetEntity(modifyEntity->delta()->Data());
    Q_ASSERT(diffEntity);
    Sink::ApplicationDomain::ApplicationDomainType diff{d->resourceContext.instanceId(), key, baseRevision, adaptorFactory->createAdaptor(*diffEntity)};
    diff.setChangedProperties(changeset.toSet());

    QByteArrayList deletions;
    if (modifyEntity->deletions()) {
        deletions = BufferUtils::fromVector(*modifyEntity->deletions());
    }

    auto preprocess = [&, this](const ApplicationDomain::ApplicationDomainType &oldEntity, ApplicationDomain::ApplicationDomainType &newEntity) {
        foreach (const auto &processor, d->processors[bufferType]) {
            processor->modifiedEntity(oldEntity, newEntity);
        }
    };

    d->revisionChanged = true;
    if (!d->entityStore.modify(bufferType, diff, deletions, replayToSource, preprocess)) {
        return KAsync::error<qint64>(0);
    }

    return KAsync::value(d->entityStore.maxRevision());
}

KAsync::Job<qint64> Pipeline::deletedEntity(void const *command, size_t size)
{
    d->transactionItemCount++;

    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command), size);
        if (!Commands::VerifyDeleteEntityBuffer(verifyer)) {
            SinkWarning() << "invalid buffer, not a delete entity buffer";
            return KAsync::error<qint64>(0);
        }
    }
    auto deleteEntity = Commands::GetDeleteEntity(command);

    const bool replayToSource = deleteEntity->replayToSource();
    const QByteArray bufferType = QByteArray(reinterpret_cast<char const *>(deleteEntity->domainType()->Data()), deleteEntity->domainType()->size());
    const QByteArray key = QByteArray(reinterpret_cast<char const *>(deleteEntity->entityId()->Data()), deleteEntity->entityId()->size());
    SinkTrace() << "Deleted Entity. Type: " << bufferType << "uid: "<< key << " replayToSource: " << replayToSource;

    auto preprocess = [&, this](const ApplicationDomain::ApplicationDomainType &oldEntity) {
        foreach (const auto &processor, d->processors[bufferType]) {
            processor->deletedEntity(oldEntity);
        }
    };

    d->revisionChanged = true;
    if (!d->entityStore.remove(bufferType, key, replayToSource, preprocess)) {
        return KAsync::error<qint64>(0);
    }

    return KAsync::value(d->entityStore.maxRevision());
}

void Pipeline::cleanupRevision(qint64 revision)
{
    d->entityStore.cleanupRevision(revision);
    d->revisionChanged = true;
}

qint64 Pipeline::cleanedUpRevision()
{
    /* return d->entityStore.cleanedUpRevision(); */
    /* return DataStore::cleanedUpRevision(d->transaction); */
    //FIXME Just move the whole cleanup revision iteration into the entitystore
    return 0;
}

qint64 Pipeline::revision()
{
    //FIXME Just move the whole cleanup revision iteration into the entitystore
    return 0;
}

class Preprocessor::Private {
public:
    QByteArray resourceType;
    QByteArray resourceInstanceIdentifier;
    Pipeline *pipeline;
};

Preprocessor::Preprocessor() : d(new Preprocessor::Private)
{
}

Preprocessor::~Preprocessor()
{
}

void Preprocessor::setup(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier, Pipeline *pipeline)
{
    d->resourceType = resourceType;
    d->resourceInstanceIdentifier = resourceInstanceIdentifier;
    d->pipeline = pipeline;
}

void Preprocessor::startBatch()
{
}

void Preprocessor::finalizeBatch()
{
}

QByteArray Preprocessor::resourceInstanceIdentifier() const
{
    return d->resourceInstanceIdentifier;
}

void Preprocessor::createEntity(const Sink::ApplicationDomain::ApplicationDomainType &entity, const QByteArray &typeName)
{
    flatbuffers::FlatBufferBuilder entityFbb;
    auto adaptorFactory = Sink::AdaptorFactoryRegistry::instance().getFactory(d->resourceType, typeName);
    adaptorFactory->createBuffer(entity, entityFbb);
    const auto entityBuffer = BufferUtils::extractBuffer(entityFbb);

    flatbuffers::FlatBufferBuilder fbb;
    auto entityId = fbb.CreateString(entity.identifier());
    auto type = fbb.CreateString(typeName);
    auto delta = Sink::EntityBuffer::appendAsVector(fbb, entityBuffer.constData(), entityBuffer.size());
    auto location = Sink::Commands::CreateCreateEntity(fbb, entityId, type, delta);
    Sink::Commands::FinishCreateEntityBuffer(fbb, location);

    const auto data = BufferUtils::extractBuffer(fbb);
    d->pipeline->newEntity(data, data.size()).exec();
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "moc_pipeline.cpp"
#pragma clang diagnostic pop
