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
#include <QStandardPaths>
#include <QVector>
#include <QDebug>
#include "entity_generated.h"
#include "metadata_generated.h"

namespace Akonadi2
{

class Pipeline::Private
{
public:
    Private(const QString &resourceName)
        : storage(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/storage", resourceName, Storage::ReadWrite),
          stepScheduled(false)
    {
    }

    Storage storage;
    QVector<Preprocessor *> nullPipeline;
    QVector<Preprocessor *> newPipeline;
    QVector<Preprocessor *> modifiedPipeline;
    QVector<Preprocessor *> deletedPipeline;
    QVector<PipelineState> activePipelines;
    bool stepScheduled;
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

Storage &Pipeline::storage() const
{
    return d->storage;
}

void Pipeline::null()
{
    //TODO: is there really any need for the null pipeline? if so, it should be doing something ;)
    PipelineState state(this, NullPipeline, QByteArray(), d->nullPipeline);
    d->activePipelines << state;
    state.step();
}

void Pipeline::newEntity(const QByteArray &key, void *resourceBufferData, size_t size)
{
    const qint64 newRevision = storage().maxRevision() + 1;


    std::vector<uint8_t> metadataData;
    //Add metadata buffer
    { 
        flatbuffers::FlatBufferBuilder metadataFbb;
        auto metadataBuilder = Akonadi2::MetadataBuilder(metadataFbb);
        metadataBuilder.add_revision(newRevision);
        auto metadataBuffer = metadataBuilder.Finish();
        Akonadi2::FinishMetadataBuffer(metadataFbb, metadataBuffer);
        metadataData.resize(metadataFbb.GetSize());
        std::copy_n(metadataFbb.GetBufferPointer(), metadataFbb.GetSize(), back_inserter(metadataData));
    }


    flatbuffers::FlatBufferBuilder fbb;
    auto metadata = fbb.CreateVector<uint8_t>(metadataData.data(), metadataData.size());
    auto resource = fbb.CreateVector<uint8_t>(static_cast<uint8_t*>(resourceBufferData), size);
    auto builder = Akonadi2::EntityBuilder(fbb);
    builder.add_metadata(metadata);
    builder.add_resource(resource);
    //We don't have a local buffer yet
    // builder.add_local();

    auto buffer = builder.Finish();
    Akonadi2::FinishEntityBuffer(fbb, buffer);

    qDebug() << "writing new entity" << key;
    storage().write(key.data(), key.size(), fbb.GetBufferPointer(), fbb.GetSize());
    storage().setMaxRevision(newRevision);

    PipelineState state(this, NewPipeline, key, d->newPipeline);
    d->activePipelines << state;
    state.step();
}

void Pipeline::modifiedEntity(const QByteArray &key, void *data, size_t size)
{
    PipelineState state(this, ModifiedPipeline, key, d->modifiedPipeline);
    d->activePipelines << state;
    state.step();
}

void Pipeline::deletedEntity(const QByteArray &key)
{
    PipelineState state(this, DeletedPipeline, key, d->deletedPipeline);
    d->activePipelines << state;
    state.step();
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
    for (PipelineState &state: d->activePipelines) {
        if (state.isIdle()) {
            state.step();
        }
    }

    d->stepScheduled = false;
}

void Pipeline::pipelineCompleted(const PipelineState &state)
{
    //TODO finalize the datastore, inform clients of the new rev
    const int index = d->activePipelines.indexOf(state);
    if (index > -1) {
        d->activePipelines.remove(index);
    }

    if (state.type() != NullPipeline) {
        emit revisionUpdated();
    }
    scheduleStep();
    if (d->activePipelines.isEmpty()) {
        emit pipelinesDrained();
    }
}


class PipelineState::Private : public QSharedData
{
public:
    Private(Pipeline *p, Pipeline::Type t, const QByteArray &k, QVector<Preprocessor *> filters)
        : pipeline(p),
          type(t),
          key(k),
          filterIt(filters),
          idle(true)
    {}

    Private()
        : pipeline(0),
          filterIt(QVector<Preprocessor *>()),
          idle(true)
    {}

    Pipeline *pipeline;
    Pipeline::Type type;
    QByteArray key;
    QVectorIterator<Preprocessor *> filterIt;
    bool idle;
};

PipelineState::PipelineState()
    : d(new Private())
{

}

PipelineState::PipelineState(Pipeline *pipeline, Pipeline::Type type, const QByteArray &key, const QVector<Preprocessor *> &filters)
    : d(new Private(pipeline, type, key, filters))
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

void PipelineState::step()
{
    if (!d->pipeline) {
        return;
    }

    d->idle = false;
    if (d->filterIt.hasNext()) {
        d->filterIt.next()->process(*this);
    } else {
        d->pipeline->pipelineCompleted(*this);
    }
}

void PipelineState::processingCompleted(Preprocessor *filter)
{
    if (d->pipeline && filter == d->filterIt.peekPrevious()) {
        d->idle = true;
        d->pipeline->pipelineStepped(*this);
    }
}

Preprocessor::Preprocessor()
    : d(0)
{
}

Preprocessor::~Preprocessor()
{
}

void Preprocessor::process(PipelineState state)
{
    processingCompleted(state);
}

void Preprocessor::processingCompleted(PipelineState state)
{
    state.processingCompleted(this);
}

} // namespace Akonadi2

