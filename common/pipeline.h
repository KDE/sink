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

#pragma once

#include <flatbuffers/flatbuffers.h>

#include <QSharedDataPointer>
#include <QObject>

#include "sink_export.h"
#include <storage.h>

#include <Async/Async>

#include "domainadaptor.h"

namespace Sink {

class Preprocessor;

class SINK_EXPORT Pipeline : public QObject
{
    Q_OBJECT

public:
    Pipeline(const ResourceContext &context);
    ~Pipeline();

    Storage::DataStore &storage() const;

    void setPreprocessors(const QString &entityType, const QVector<Preprocessor *> &preprocessors);
    void startTransaction();
    void commit();
    Storage::DataStore::Transaction &transaction();

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

signals:
    void revisionUpdated(qint64);

private:
    class Private;
    const std::unique_ptr<Private> d;
};

class SINK_EXPORT Preprocessor
{
public:
    Preprocessor();
    virtual ~Preprocessor();

    virtual void startBatch();
    virtual void newEntity(const QByteArray &uid, qint64 revision, ApplicationDomain::BufferAdaptor &newEntity, Storage::DataStore::Transaction &transaction) {};
    virtual void modifiedEntity(const QByteArray &uid, qint64 revision, const ApplicationDomain::BufferAdaptor &oldEntity,
        ApplicationDomain::BufferAdaptor &newEntity, Storage::DataStore::Transaction &transaction) {};
    virtual void deletedEntity(const QByteArray &uid, qint64 revision, const ApplicationDomain::BufferAdaptor &oldEntity, Storage::DataStore::Transaction &transaction) {};
    virtual void finalize();

    void setup(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier, Pipeline *);

protected:
    template <typename DomainType>
    void createEntity(const DomainType &entity)
    {
        createEntity(entity, ApplicationDomain::getTypeName<DomainType>());
    }
    void createEntity(const ApplicationDomain::ApplicationDomainType &entity, const QByteArray &type);

    QByteArray resourceInstanceIdentifier() const;

private:
    friend class Pipeline;
    class Private;
    const std::unique_ptr<Private> d;
};

template<typename DomainType>
class SINK_EXPORT EntityPreprocessor: public Preprocessor
{
public:
    virtual void newEntity(DomainType &, Storage::DataStore::Transaction &transaction) {};
    virtual void modifiedEntity(const DomainType &oldEntity, DomainType &newEntity, Storage::DataStore::Transaction &transaction) {};
    virtual void deletedEntity(const DomainType &oldEntity, Storage::DataStore::Transaction &transaction) {};

private:
    static void nullDeleter(ApplicationDomain::BufferAdaptor *) {}
    virtual void newEntity(const QByteArray &uid, qint64 revision, ApplicationDomain::BufferAdaptor &bufferAdaptor, Storage::DataStore::Transaction &transaction)  Q_DECL_OVERRIDE
    {
        auto o = DomainType("", uid, revision, QSharedPointer<ApplicationDomain::BufferAdaptor>(&bufferAdaptor, nullDeleter));
        newEntity(o, transaction);
    }

    virtual void modifiedEntity(const QByteArray &uid, qint64 revision, const ApplicationDomain::BufferAdaptor &oldEntity,
        ApplicationDomain::BufferAdaptor &bufferAdaptor, Storage::DataStore::Transaction &transaction) Q_DECL_OVERRIDE
    {
        auto o = DomainType("", uid, revision, QSharedPointer<ApplicationDomain::BufferAdaptor>(&bufferAdaptor, nullDeleter));
        modifiedEntity(DomainType("", uid, 0, QSharedPointer<ApplicationDomain::BufferAdaptor>(const_cast<ApplicationDomain::BufferAdaptor*>(&oldEntity), nullDeleter)), o, transaction);
    }
    virtual void deletedEntity(const QByteArray &uid, qint64 revision, const ApplicationDomain::BufferAdaptor &bufferAdaptor, Storage::DataStore::Transaction &transaction) Q_DECL_OVERRIDE
    {
        deletedEntity(DomainType("", uid, revision, QSharedPointer<ApplicationDomain::BufferAdaptor>(const_cast<ApplicationDomain::BufferAdaptor*>(&bufferAdaptor), nullDeleter)), transaction);
    }
};

} // namespace Sink
