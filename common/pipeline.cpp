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
#include "domain/applicationdomaintype_p.h"
#include "adaptorfactoryregistry.h"
#include "definitions.h"
#include "bufferutils.h"
#include "storage/entitystore.h"
#include "store.h"

using namespace Sink;
using namespace Sink::Storage;

class Pipeline::Private
{
public:
    Private(const ResourceContext &context, const Sink::Log::Context &ctx) : logCtx{ctx.subContext("pipeline")}, resourceContext(context), entityStore(context, ctx), revisionChanged(false)
    {
    }

    Sink::Log::Context logCtx;
    ResourceContext resourceContext;
    Storage::EntityStore entityStore;
    QHash<QString, QVector<QSharedPointer<Preprocessor>>> processors;
    bool revisionChanged;
    QTime transactionTime;
    int transactionItemCount;
};


Pipeline::Pipeline(const ResourceContext &context, const Sink::Log::Context &ctx) : QObject(nullptr), d(new Private(context, ctx))
{
    //Create main store immediately on first start
    d->entityStore.startTransaction(DataStore::ReadWrite);
    d->entityStore.commitTransaction();
}

Pipeline::~Pipeline()
{
}

void Pipeline::setPreprocessors(const QString &entityType, const QVector<Preprocessor *> &processors)
{
    auto &list = d->processors[entityType];
    list.clear();
    for (auto p : processors) {
        p->setup(d->resourceContext.resourceType, d->resourceContext.instanceId(), this, &d->entityStore);
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
    SinkTraceCtx(d->logCtx) << "Starting transaction.";
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
    SinkTraceCtx(d->logCtx) << "Committing revision: " << revision << ":" << d->transactionItemCount << " items in: " << Log::TraceTime(elapsed) << " "
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
            SinkWarningCtx(d->logCtx) << "invalid buffer, not a create entity buffer";
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
            SinkErrorCtx(d->logCtx) << "An entity with this id already exists: " << key;
            return KAsync::error<qint64>(0);
        }
    }

    if (key.isEmpty()) {
        key = DataStore::generateUid();
    }
    SinkTraceCtx(d->logCtx) << "New Entity. Type: " << bufferType << "uid: "<< key << " replayToSource: " << replayToSource;
    Q_ASSERT(!key.isEmpty());

    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(createEntity->delta()->Data()), createEntity->delta()->size());
        if (!VerifyEntityBuffer(verifyer)) {
            SinkWarningCtx(d->logCtx) << "invalid buffer, not an entity buffer";
            return KAsync::error<qint64>(0);
        }
    }
    auto entity = GetEntity(createEntity->delta()->Data());
    if (!entity->resource()->size() && !entity->local()->size()) {
        SinkWarningCtx(d->logCtx) << "No local and no resource buffer while trying to create entity.";
        return KAsync::error<qint64>(0);
    }

    auto adaptorFactory = Sink::AdaptorFactoryRegistry::instance().getFactory(d->resourceContext.resourceType, bufferType);
    if (!adaptorFactory) {
        SinkWarningCtx(d->logCtx) << "no adaptor factory for type " << bufferType;
        return KAsync::error<qint64>(0);
    }

    auto adaptor = adaptorFactory->createAdaptor(*entity);
    auto memoryAdaptor = QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create();
    Sink::ApplicationDomain::copyBuffer(*adaptor, *memoryAdaptor);

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

template <class T>
struct CreateHelper {
    KAsync::Job<void> operator()(const ApplicationDomain::ApplicationDomainType &arg) const {
        return Sink::Store::create<T>(T{arg});
    }
};

KAsync::Job<qint64> Pipeline::modifiedEntity(void const *command, size_t size)
{
    d->transactionItemCount++;

    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command), size);
        if (!Commands::VerifyModifyEntityBuffer(verifyer)) {
            SinkWarningCtx(d->logCtx) << "invalid buffer, not a modify entity buffer";
            return KAsync::error<qint64>(0);
        }
    }
    auto modifyEntity = Commands::GetModifyEntity(command);
    Q_ASSERT(modifyEntity);
    QList<QByteArray> changeset;
    if (modifyEntity->modifiedProperties()) {
        changeset = BufferUtils::fromVector(*modifyEntity->modifiedProperties());
    } else {
        SinkWarningCtx(d->logCtx) << "No changeset available";
    }
    const qint64 baseRevision = modifyEntity->revision();
    const bool replayToSource = modifyEntity->replayToSource();

    const QByteArray bufferType = QByteArray(reinterpret_cast<char const *>(modifyEntity->domainType()->Data()), modifyEntity->domainType()->size());
    const QByteArray key = QByteArray(reinterpret_cast<char const *>(modifyEntity->entityId()->Data()), modifyEntity->entityId()->size());
    SinkTraceCtx(d->logCtx) << "Modified Entity. Type: " << bufferType << "uid: "<< key << " replayToSource: " << replayToSource;
    if (bufferType.isEmpty() || key.isEmpty()) {
        SinkWarningCtx(d->logCtx) << "entity type or key " << bufferType << key;
        return KAsync::error<qint64>(0);
    }
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(modifyEntity->delta()->Data()), modifyEntity->delta()->size());
        if (!VerifyEntityBuffer(verifyer)) {
            SinkWarningCtx(d->logCtx) << "invalid buffer, not an entity buffer";
            return KAsync::error<qint64>(0);
        }
    }

    auto adaptorFactory = Sink::AdaptorFactoryRegistry::instance().getFactory(d->resourceContext.resourceType, bufferType);
    if (!adaptorFactory) {
        SinkWarningCtx(d->logCtx) << "no adaptor factory for type " << bufferType;
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

    if (modifyEntity->targetResource()) {
        auto isMove = modifyEntity->removeEntity();
        auto targetResource = BufferUtils::extractBuffer(modifyEntity->targetResource());
        auto changeset = diff.changedProperties();
        const auto current = d->entityStore.readLatest(bufferType, diff.identifier());
        if (current.identifier().isEmpty()) {
            SinkWarningCtx(d->logCtx) << "Failed to read current version: " << diff.identifier();
            return KAsync::error<qint64>(0);
        }

        auto newEntity = *ApplicationDomain::ApplicationDomainType::getInMemoryCopy<ApplicationDomain::ApplicationDomainType>(current, current.availableProperties());

        // Apply diff
        for (const auto &property : changeset) {
            const auto value = diff.getProperty(property);
            if (value.isValid()) {
                newEntity.setProperty(property, value);
            }
        }

        // Remove deletions
        for (const auto &property : deletions) {
            newEntity.setProperty(property, QVariant());
        }
        newEntity.setResource(targetResource);
        newEntity.setChangedProperties(newEntity.availableProperties().toSet());

        SinkTraceCtx(d->logCtx) << "Moving entity to new resource " << newEntity.identifier() << newEntity.resourceInstanceIdentifier() << targetResource;
        auto job = TypeHelper<CreateHelper>{bufferType}.operator()<KAsync::Job<void>, ApplicationDomain::ApplicationDomainType&>(newEntity);
        job = job.then([this, current, isMove, targetResource, bufferType](const KAsync::Error &error) {
            if (!error) {
                SinkTraceCtx(d->logCtx) << "Move of " << current.identifier() << "was successfull";
                if (isMove) {
                    startTransaction();
                    flatbuffers::FlatBufferBuilder fbb;
                    auto entityId = fbb.CreateString(current.identifier());
                    auto type = fbb.CreateString(bufferType);
                    auto location = Sink::Commands::CreateDeleteEntity(fbb, current.revision(), entityId, type, true);
                    Sink::Commands::FinishDeleteEntityBuffer(fbb, location);
                    const auto data = BufferUtils::extractBuffer(fbb);
                    deletedEntity(data, data.size()).exec();
                    commit();
                }
            } else {
                SinkErrorCtx(d->logCtx) << "Failed to move entity " << targetResource << " to resource " << current.identifier();
            }
        });
        job.exec();
        return KAsync::value<qint64>(0);
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
            SinkWarningCtx(d->logCtx) << "invalid buffer, not a delete entity buffer";
            return KAsync::error<qint64>(0);
        }
    }
    auto deleteEntity = Commands::GetDeleteEntity(command);

    const bool replayToSource = deleteEntity->replayToSource();
    const QByteArray bufferType = QByteArray(reinterpret_cast<char const *>(deleteEntity->domainType()->Data()), deleteEntity->domainType()->size());
    const QByteArray key = QByteArray(reinterpret_cast<char const *>(deleteEntity->entityId()->Data()), deleteEntity->entityId()->size());
    SinkTraceCtx(d->logCtx) << "Deleted Entity. Type: " << bufferType << "uid: "<< key << " replayToSource: " << replayToSource;

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

void Pipeline::cleanupRevisions(qint64 revision)
{
    //We have to set revisionChanged, otherwise a call to commit might abort
    //the transaction when not using the implicit internal transaction
    d->revisionChanged = d->entityStore.cleanupRevisions(revision);
}


class Preprocessor::Private {
public:
    QByteArray resourceType;
    QByteArray resourceInstanceIdentifier;
    Pipeline *pipeline;
    Storage::EntityStore *entityStore;
};

Preprocessor::Preprocessor() : d(new Preprocessor::Private)
{
}

Preprocessor::~Preprocessor()
{
}

void Preprocessor::setup(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier, Pipeline *pipeline, Storage::EntityStore *entityStore)
{
    d->resourceType = resourceType;
    d->resourceInstanceIdentifier = resourceInstanceIdentifier;
    d->pipeline = pipeline;
    d->entityStore = entityStore;
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

Storage::EntityStore &Preprocessor::entityStore() const
{
    return *d->entityStore;
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
