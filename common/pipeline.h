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

#include "sinkcommon_export.h"
#include <storage.h>

#include <Async/Async>

#include "domainadaptor.h"

namespace Sink
{

class Preprocessor;

class SINKCOMMON_EXPORT Pipeline : public QObject
{
    Q_OBJECT

public:
    Pipeline(const QString &storagePath, QObject *parent = 0);
    ~Pipeline();

    Storage &storage() const;

    void setPreprocessors(const QString &entityType, const QVector<Preprocessor *> &preprocessors);
    void startTransaction();
    void commit();
    Storage::Transaction &transaction();

    void setAdaptorFactory(const QString &entityType, DomainTypeAdaptorFactoryInterface::Ptr factory);

    KAsync::Job<qint64> newEntity(void const *command, size_t size);
    KAsync::Job<qint64> modifiedEntity(void const *command, size_t size);
    KAsync::Job<qint64> deletedEntity(void const *command, size_t size);
    /*
     * Cleans up a single revision.
     *
     * This has to be called for every revision in consecutive order.
     */
    void cleanupRevision(qint64 revision);

    /*
     * Returns the latest cleaned up revision.
     */
    qint64 cleanedUpRevision();

Q_SIGNALS:
    void revisionUpdated(qint64);

private:
    class Private;
    Private * const d;
};

class SINKCOMMON_EXPORT Preprocessor
{
public:
    Preprocessor();
    virtual ~Preprocessor();

    virtual void startBatch();
    virtual void newEntity(const QByteArray &key, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &newEntity, Sink::Storage::Transaction &transaction) = 0;
    virtual void modifiedEntity(const QByteArray &key, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, const Sink::ApplicationDomain::BufferAdaptor &newEntity, Sink::Storage::Transaction &transaction) = 0;
    virtual void deletedEntity(const QByteArray &key, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::Storage::Transaction &transaction) = 0;
    virtual void finalize();

private:
    class Private;
    Private * const d;
};

} // namespace Sink

