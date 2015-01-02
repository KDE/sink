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
#include <clientapi.h> //For domain types

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

    // template <typename T>
    // Storage &storage() const;

    template <typename T>
    void setPreprocessors(Type type, const QVector<Preprocessor *> &preprocessors)
    {
        setPreprocessors(Akonadi2::Domain::getTypeName<T>(), type, preprocessors);
    }

    void null();

    template <typename T>
    void newEntity(const QByteArray &key, void *resourceBufferData, size_t size)
    {
        newEntity(Akonadi2::Domain::getTypeName<T>(), key, resourceBufferData, size);
    }

    template <typename T>
    void modifiedEntity(const QByteArray &key, void *data, size_t size)
    {
        modifiedEntity(Akonadi2::Domain::getTypeName<T>(), key, data, size);
    }

    template <typename T>
    void deletedEntity(const QByteArray &key)
    {
        deletedEntity(Akonadi2::Domain::getTypeName<T>(), key);
    }

Q_SIGNALS:
    void revisionUpdated();
    void pipelinesDrained();

private Q_SLOTS:
    void stepPipelines();

private:
    void setPreprocessors(const QString &entityType, Type pipelineType, const QVector<Preprocessor *> &preprocessors);
    void newEntity(const QString &entityType, const QByteArray &key, void *resourceBufferData, size_t size);
    void modifiedEntity(const QString &entityType, const QByteArray &key, void *data, size_t size);
    void deletedEntity(const QString &entityType, const QByteArray &key);
    void pipelineStepped(const PipelineState &state);
    void pipelineCompleted(const PipelineState &state);
    void scheduleStep();

    friend class PipelineState;

    class Private;
    Private * const d;
};

class AKONADI2COMMON_EXPORT PipelineState
{
public:
    PipelineState();
    PipelineState(Pipeline *pipeline, Pipeline::Type type, const QByteArray &key, const QVector<Preprocessor *> &filters);
    PipelineState(const PipelineState &other);
    ~PipelineState();

    PipelineState &operator=(const PipelineState &rhs);
    bool operator==(const PipelineState &rhs);

    bool isIdle() const;
    QByteArray key() const;
    Pipeline::Type type() const;

    void step();
    void processingCompleted(Preprocessor *filter);

private:
    class Private;
    QExplicitlySharedDataPointer<Private> d;
};

class AKONADI2COMMON_EXPORT Preprocessor
{
public:
    Preprocessor();
    virtual ~Preprocessor();

    virtual void process(PipelineState state);

protected:
    void processingCompleted(PipelineState state);

private:
    class Private;
    Private * const d;
};

} // namespace Akonadi2

