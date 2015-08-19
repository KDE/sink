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

#pragma once

#include <flatbuffers/flatbuffers.h>

#include <QSharedDataPointer>
#include <QObject>

#include <akonadi2common_export.h>
#include <storage.h>

#include <Async/Async>

#include "domainadaptor.h"

namespace Akonadi2
{

class PipelineState;
class Preprocessor;

class AKONADI2COMMON_EXPORT Pipeline : public QObject
{
    Q_OBJECT

public:
    enum Type { NullPipeline, NewPipeline, ModifiedPipeline, DeletedPipeline };

    Pipeline(const QString &storagePath, QObject *parent = 0);
    ~Pipeline();

    Storage &storage() const;

    void setPreprocessors(const QString &entityType, Type pipelineType, const QVector<Preprocessor *> &preprocessors);
    void startTransaction();
    void commit();
    Storage::Transaction &transaction();

    void null();
    void setAdaptorFactory(const QString &entityType, DomainTypeAdaptorFactoryInterface::Ptr factory);

    KAsync::Job<void> newEntity(void const *command, size_t size);
    KAsync::Job<void> modifiedEntity(void const *command, size_t size);
    KAsync::Job<void> deletedEntity(void const *command, size_t size);

Q_SIGNALS:
    void revisionUpdated(qint64);
    void pipelinesDrained();

private Q_SLOTS:
    void stepPipelines();

private:
    void pipelineStepped(const PipelineState &state);
    //Don't use a reference here (it would invalidate itself)
    void pipelineCompleted(PipelineState state);
    void scheduleStep();

    friend class PipelineState;

    class Private;
    Private * const d;
};

class AKONADI2COMMON_EXPORT PipelineState
{
public:
    PipelineState();
    PipelineState(Pipeline *pipeline, Pipeline::Type type, const QByteArray &key, const QVector<Preprocessor *> &filters, qint64 revision, const std::function<void()> &callback);
    PipelineState(const PipelineState &other);
    ~PipelineState();

    PipelineState &operator=(const PipelineState &rhs);
    bool operator==(const PipelineState &rhs);

    bool isIdle() const;
    QByteArray key() const;
    Pipeline::Type type() const;
    qint64 revision() const;
    //TODO expose command

    void step();
    void processingCompleted(Preprocessor *filter);

    void callback();

private:
    class Private;
    QExplicitlySharedDataPointer<Private> d;
};

class AKONADI2COMMON_EXPORT Preprocessor
{
public:
    Preprocessor();
    virtual ~Preprocessor();

    //TODO pass actual command as well, for changerecording
    virtual void process(const PipelineState &state, const Akonadi2::Entity &);
    //TODO to record progress
    virtual QString id() const;

protected:
    void processingCompleted(PipelineState state);

private:
    class Private;
    Private * const d;
};

/**
 * A simple processor that takes a single function
 */
class AKONADI2COMMON_EXPORT SimpleProcessor : public Akonadi2::Preprocessor
{
public:
    SimpleProcessor(const QString &id, const std::function<void(const Akonadi2::PipelineState &state, const Akonadi2::Entity &e)> &f)
        : Akonadi2::Preprocessor(),
        mFunction(f),
        mId(id)
    {
    }

    void process(const Akonadi2::PipelineState &state, const Akonadi2::Entity &e) Q_DECL_OVERRIDE
    {
        mFunction(state, e);
        processingCompleted(state);
    }

    QString id() const
    {
        return mId;
    }

protected:
    std::function<void(const Akonadi2::PipelineState &state, const Akonadi2::Entity &e)> mFunction;
    QString mId;
};

} // namespace Akonadi2

