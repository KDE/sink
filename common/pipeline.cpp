/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
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

namespace Akonadi2
{

class Pipeline::Private
{
public:
    Private(const QString &resourceName)
        : storage(Akonadi2::storageLocation(), resourceName, Storage::ReadWrite),
          stepScheduled(false)
    {
    }

    Storage storage;
    Storage::Transaction transaction;
    QHash<QString, QVector<Preprocessor *> > nullPipeline;
    QHash<QString, QVector<Preprocessor *> > newPipeline;
    QHash<QString, QVector<Preprocessor *> > modifiedPipeline;
    QHash<QString, QVector<Preprocessor *> > deletedPipeline;
    QVector<PipelineState> activePipelines;
    bool stepScheduled;
    QHash<QString, DomainTypeAdaptorFactoryInterface::Ptr> adaptorFactory;
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

void Pipeline::setPreprocessors(const QString &entityType, Type pipelineType, const QVector<Preprocessor *> &preprocessors)
{
    switch (pipelineType) {
        case NewPipeline:
            d->newPipeline[entityType] = preprocessors;
            break;
        case ModifiedPipeline:
            d->modifiedPipeline[entityType] = preprocessors;
            break;
        case DeletedPipeline:
            d->deletedPipeline[entityType] = preprocessors;
            break;
        default:
            break;
    };
}

void Pipeline::setAdaptorFactory(const QString &entityType, DomainTypeAdaptorFactoryInterface::Ptr factory)
{
    d->adaptorFactory.insert(entityType, factory);
}

void Pipeline::startTransaction()
{
    if (d->transaction) {
        return;
    }
    d->transaction = std::move(storage().createTransaction(Akonadi2::Storage::ReadWrite));
}

void Pipeline::commit()
{
    if (d->transaction) {
        d->transaction.commit();
    }
    d->transaction = Storage::Transaction();
}

Storage::Transaction &Pipeline::transaction()
{
    return d->transaction;
}

Storage &Pipeline::storage() const
{
    return d->storage;
}

void Pipeline::null()
{
    //TODO: is there really any need for the null pipeline? if so, it should be doing something ;)
    // PipelineState state(this, NullPipeline, QByteArray(), d->nullPipeline);
    // d->activePipelines << state;
    // state.step();
}

KAsync::Job<void> Pipeline::newEntity(void const *command, size_t size)
{
    Log() << "Pipeline: New Entity";

    //TODO toRFC4122 would probably be more efficient, but results in non-printable keys.
    const auto key = QUuid::createUuid().toString().toUtf8();

    const qint64 newRevision = Akonadi2::Storage::maxRevision(d->transaction) + 1;

    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command), size);
        if (!Akonadi2::Commands::VerifyCreateEntityBuffer(verifyer)) {
            Warning() << "invalid buffer, not a create entity buffer";
            return KAsync::error<void>();
        }
    }
    auto createEntity = Akonadi2::Commands::GetCreateEntity(command);

    //TODO rename createEntitiy->domainType to bufferType
    const QByteArray bufferType = QByteArray(reinterpret_cast<char const*>(createEntity->domainType()->Data()), createEntity->domainType()->size());
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(createEntity->delta()->Data()), createEntity->delta()->size());
        if (!Akonadi2::VerifyEntityBuffer(verifyer)) {
            Warning() << "invalid buffer, not an entity buffer";
            return KAsync::error<void>();
        }
    }
    auto entity = Akonadi2::GetEntity(createEntity->delta()->Data());
    if (!entity->resource()->size() && !entity->local()->size()) {
        Warning() << "No local and no resource buffer while trying to create entity.";
        return KAsync::error<void>();
    }

    //Add metadata buffer
    flatbuffers::FlatBufferBuilder metadataFbb;
    auto metadataBuilder = Akonadi2::MetadataBuilder(metadataFbb);
    metadataBuilder.add_revision(newRevision);
    metadataBuilder.add_processed(false);
    auto metadataBuffer = metadataBuilder.Finish();
    Akonadi2::FinishMetadataBuffer(metadataFbb, metadataBuffer);
    //TODO we should reserve some space in metadata for in-place updates

    flatbuffers::FlatBufferBuilder fbb;
    EntityBuffer::assembleEntityBuffer(fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize(), entity->resource()->Data(), entity->resource()->size(), entity->local()->Data(), entity->local()->size());

    d->transaction.openDatabase(bufferType + ".main").write(key, QByteArray::fromRawData(reinterpret_cast<char const *>(fbb.GetBufferPointer()), fbb.GetSize()));
    Akonadi2::Storage::setMaxRevision(d->transaction, newRevision);
    Log() << "Pipeline: wrote entity: " << key << newRevision << bufferType;

    return KAsync::start<void>([this, key, bufferType, newRevision](KAsync::Future<void> &future) {
        PipelineState state(this, NewPipeline, key, d->newPipeline[bufferType], newRevision, [&future]() {
            future.setFinished();
        }, bufferType);
        d->activePipelines << state;
        state.step();
    });
}

KAsync::Job<void> Pipeline::modifiedEntity(void const *command, size_t size)
{
    Log() << "Pipeline: Modified Entity";

    const qint64 newRevision = Akonadi2::Storage::maxRevision(d->transaction) + 1;

    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command), size);
        if (!Akonadi2::Commands::VerifyModifyEntityBuffer(verifyer)) {
            Warning() << "invalid buffer, not a modify entity buffer";
            return KAsync::error<void>();
        }
    }
    auto modifyEntity = Akonadi2::Commands::GetModifyEntity(command);
    Q_ASSERT(modifyEntity);

    //TODO rename modifyEntity->domainType to bufferType
    const QByteArray bufferType = QByteArray(reinterpret_cast<char const*>(modifyEntity->domainType()->Data()), modifyEntity->domainType()->size());
    const QByteArray key = QByteArray(reinterpret_cast<char const*>(modifyEntity->entityId()->Data()), modifyEntity->entityId()->size());
    if (bufferType.isEmpty() || key.isEmpty()) {
        Warning() << "entity type or key " << bufferType << key;
        return KAsync::error<void>();
    }
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(modifyEntity->delta()->Data()), modifyEntity->delta()->size());
        if (!Akonadi2::VerifyEntityBuffer(verifyer)) {
            Warning() << "invalid buffer, not an entity buffer";
            return KAsync::error<void>();
        }
    }

    auto adaptorFactory = d->adaptorFactory.value(bufferType);
    if (!adaptorFactory) {
        Warning() << "no adaptor factory for type " << bufferType;
        return KAsync::error<void>();
    }

    auto diffEntity = Akonadi2::GetEntity(modifyEntity->delta()->Data());
    Q_ASSERT(diffEntity);
    auto diff = adaptorFactory->createAdaptor(*diffEntity);

    QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor> current;
    storage().createTransaction(Akonadi2::Storage::ReadOnly).openDatabase(bufferType + ".main").scan(key, [&current, adaptorFactory](const QByteArray &key, const QByteArray &data) -> bool {
        Akonadi2::EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
        if (!buffer.isValid()) {
            Warning() << "Read invalid buffer from disk";
        } else {
            current = adaptorFactory->createAdaptor(buffer.entity());
        }
        return false;
    },
    [](const Storage::Error &error) {
        Warning() << "Failed to read value from storage: " << error.message;
    });
    //TODO error handler

    if (!current) {
        Warning() << "Failed to read local value " << key;
        return KAsync::error<void>();
    }

    //resource and uid don't matter at this point
    const Akonadi2::ApplicationDomain::ApplicationDomainType existingObject("", "", newRevision, current);
    auto newObject = Akonadi2::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<Akonadi2::ApplicationDomain::ApplicationDomainType>(existingObject);

    //Apply diff
    //FIXME only apply the properties that are available in the buffer
    for (const auto &property : diff->availableProperties()) {
        newObject->setProperty(property, diff->getProperty(property));
    }

    //Remove deletions
    if (modifyEntity->deletions()) {
        for (const auto &property : *modifyEntity->deletions()) {
            newObject->setProperty(QByteArray::fromRawData(property->data(), property->size()), QVariant());
        }
    }

    //Add metadata buffer
    flatbuffers::FlatBufferBuilder metadataFbb;
    auto metadataBuilder = Akonadi2::MetadataBuilder(metadataFbb);
    metadataBuilder.add_revision(newRevision);
    metadataBuilder.add_processed(false);
    auto metadataBuffer = metadataBuilder.Finish();
    Akonadi2::FinishMetadataBuffer(metadataFbb, metadataBuffer);

    flatbuffers::FlatBufferBuilder fbb;
    adaptorFactory->createBuffer(*newObject, fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize());

    //TODO don't overwrite the old entry, but instead store a new revision
    d->transaction.openDatabase(bufferType + ".main").write(key, QByteArray::fromRawData(reinterpret_cast<char const *>(fbb.GetBufferPointer()), fbb.GetSize()));
    Akonadi2::Storage::setMaxRevision(d->transaction, newRevision);

    return KAsync::start<void>([this, key, bufferType, newRevision](KAsync::Future<void> &future) {
        PipelineState state(this, ModifiedPipeline, key, d->modifiedPipeline[bufferType], newRevision, [&future]() {
            future.setFinished();
        }, bufferType);
        d->activePipelines << state;
        state.step();
    });
}

KAsync::Job<void> Pipeline::deletedEntity(void const *command, size_t size)
{
    Log() << "Pipeline: Deleted Entity";

    const qint64 newRevision = Akonadi2::Storage::maxRevision(d->transaction) + 1;

    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command), size);
        if (!Akonadi2::Commands::VerifyDeleteEntityBuffer(verifyer)) {
            Warning() << "invalid buffer, not a delete entity buffer";
            return KAsync::error<void>();
        }
    }
    auto deleteEntity = Akonadi2::Commands::GetDeleteEntity(command);

    const QByteArray bufferType = QByteArray(reinterpret_cast<char const*>(deleteEntity->domainType()->Data()), deleteEntity->domainType()->size());
    const QByteArray key = QByteArray(reinterpret_cast<char const*>(deleteEntity->entityId()->Data()), deleteEntity->entityId()->size());

    //TODO instead of deleting the entry, a new revision should be created that marks the entity as deleted
    d->transaction.openDatabase(bufferType + ".main").remove(key);
    Akonadi2::Storage::setMaxRevision(d->transaction, newRevision);
    Log() << "Pipeline: deleted entity: "<< newRevision;

    return KAsync::start<void>([this, key, bufferType, newRevision](KAsync::Future<void> &future) {
        PipelineState state(this, DeletedPipeline, key, d->deletedPipeline[bufferType], newRevision, [&future](){
            future.setFinished();
        }, bufferType);
        d->activePipelines << state;
        state.step();
    });
}

void Pipeline::pipelineStepped(const PipelineState &state)
{
    scheduleStep();
}

void Pipeline::scheduleStep()
{
    if (!d->stepScheduled) {
        d->stepScheduled = true;
        QMetaObject::invokeMethod(this, "stepPipelines", Qt::QueuedConnection);
    }
}

void Pipeline::stepPipelines()
{
    d->stepScheduled = false;
    for (PipelineState &state: d->activePipelines) {
        if (state.isIdle()) {
            state.step();
        }
    }
}

void Pipeline::pipelineCompleted(PipelineState state)
{
    //TODO finalize the datastore, inform clients of the new rev
    const int index = d->activePipelines.indexOf(state);
    if (index > -1) {
        d->activePipelines.remove(index);
    }
    state.callback();

    if (state.type() != NullPipeline) {
        emit revisionUpdated(state.revision());
    }
    scheduleStep();
    if (d->activePipelines.isEmpty()) {
        emit pipelinesDrained();
    }
}


class PipelineState::Private : public QSharedData
{
public:
    Private(Pipeline *p, Pipeline::Type t, const QByteArray &k, QVector<Preprocessor *> filters, const std::function<void()> &c, qint64 r, const QByteArray &b)
        : pipeline(p),
          type(t),
          key(k),
          filterIt(filters),
          idle(true),
          callback(c),
          revision(r),
          bufferType(b)
    {}

    Private()
        : pipeline(0),
          filterIt(QVector<Preprocessor *>()),
          idle(true),
          revision(-1)
    {}

    Pipeline *pipeline;
    Pipeline::Type type;
    QByteArray key;
    QVectorIterator<Preprocessor *> filterIt;
    bool idle;
    std::function<void()> callback;
    qint64 revision;
    QByteArray bufferType;
};

PipelineState::PipelineState()
    : d(new Private())
{

}

PipelineState::PipelineState(Pipeline *pipeline, Pipeline::Type type, const QByteArray &key, const QVector<Preprocessor *> &filters, qint64 revision, const std::function<void()> &callback, const QByteArray &bufferType)
    : d(new Private(pipeline, type, key, filters, callback, revision, bufferType))
{
}

PipelineState::PipelineState(const PipelineState &other)
    : d(other.d)
{
}

PipelineState::~PipelineState()
{
}

PipelineState &PipelineState::operator=(const PipelineState &rhs)
{
    d = rhs.d;
    return *this;
}

bool PipelineState::operator==(const PipelineState &rhs)
{
    return d == rhs.d;
}

bool PipelineState::isIdle() const
{
    return d->idle;
}

QByteArray PipelineState::key() const
{
    return d->key;
}

Pipeline::Type PipelineState::type() const
{
    return d->type;
}

qint64 PipelineState::revision() const
{
    return d->revision;
}

QByteArray PipelineState::bufferType() const
{
    return d->bufferType;
}

void PipelineState::step()
{
    if (!d->pipeline) {
        Q_ASSERT(false);
        return;
    }

    d->idle = false;
    if (d->filterIt.hasNext()) {
        //TODO skip step if already processed
        auto preprocessor = d->filterIt.next();
        preprocessor->process(*this, d->pipeline->transaction());
    } else {
        //This object becomes invalid after this call
        d->pipeline->pipelineCompleted(*this);
    }
}

void PipelineState::processingCompleted(Preprocessor *filter)
{
    //TODO record processing progress
    if (d->pipeline && filter == d->filterIt.peekPrevious()) {
        d->idle = true;
        d->pipeline->pipelineStepped(*this);
    }
}

void  PipelineState::callback()
{
    d->callback();
}


Preprocessor::Preprocessor()
    : d(0)
{
}

Preprocessor::~Preprocessor()
{
}

void Preprocessor::process(const PipelineState &state, Akonadi2::Storage::Transaction &transaction)
{
    processingCompleted(state);
}

void Preprocessor::processingCompleted(PipelineState state)
{
    state.processingCompleted(this);
}

QString Preprocessor::id() const
{
    return QLatin1String("unknown processor");
}

} // namespace Akonadi2

