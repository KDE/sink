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

#include <KAsync/Async>

#include <bufferadaptor.h>
#include <resourcecontext.h>

namespace Sink {
namespace Storage {
    class EntityStore;
}

class Preprocessor;

class SINK_EXPORT Pipeline : public QObject
{
    Q_OBJECT

public:
    Pipeline(const ResourceContext &context, const Sink::Log::Context &ctx);
    ~Pipeline();

    void setPreprocessors(const QString &entityType, const QVector<Preprocessor *> &preprocessors);
    void startTransaction();
    void commit();

    KAsync::Job<qint64> newEntity(void const *command, size_t size);
    KAsync::Job<qint64> modifiedEntity(void const *command, size_t size);
    KAsync::Job<qint64> deletedEntity(void const *command, size_t size);

    /*
     * Cleans up all revisions until @param revision.
     */
    void cleanupRevisions(qint64 revision);


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

    enum Action {
        NoAction,
        MoveToResource,
        CopyToResource,
        DropModification,
        DeleteEntity
    };

    enum Type {
        Creation,
        Modification,
        Deletion
    };
    struct Result {
        Action action;
    };

    virtual void startBatch();
    virtual void newEntity(ApplicationDomain::ApplicationDomainType &newEntity);
    virtual void modifiedEntity(const ApplicationDomain::ApplicationDomainType &oldEntity, ApplicationDomain::ApplicationDomainType &newEntity);
    virtual void deletedEntity(const ApplicationDomain::ApplicationDomainType &oldEntity);
    virtual Result processModification(Type type, const ApplicationDomain::ApplicationDomainType &current, ApplicationDomain::ApplicationDomainType &diff);
    virtual void finalizeBatch();

    void setup(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier, Pipeline *, Storage::EntityStore *entityStore);

protected:
    template <typename DomainType>
    void createEntity(const DomainType &entity)
    {
        createEntity(entity, ApplicationDomain::getTypeName<DomainType>());
    }
    void createEntity(const ApplicationDomain::ApplicationDomainType &entity, const QByteArray &type);

    QByteArray resourceInstanceIdentifier() const;

    Storage::EntityStore &entityStore() const;

private:
    friend class Pipeline;
    class Private;
    const std::unique_ptr<Private> d;
};

template<typename DomainType>
class SINK_EXPORT EntityPreprocessor: public Preprocessor
{
public:
    virtual void newEntity(DomainType &) {};
    virtual void modifiedEntity(const DomainType &oldEntity, DomainType &newEntity) {};
    virtual void deletedEntity(const DomainType &oldEntity) {};

private:
    virtual void newEntity(ApplicationDomain::ApplicationDomainType &newEntity_)  Q_DECL_OVERRIDE
    {
        //Modifications still work due to the underlying shared adaptor
        auto newEntityCopy = DomainType(newEntity_);
        newEntity(newEntityCopy);
    }

    virtual void modifiedEntity(const ApplicationDomain::ApplicationDomainType &oldEntity, ApplicationDomain::ApplicationDomainType &newEntity_) Q_DECL_OVERRIDE
    {
        //Modifications still work due to the underlying shared adaptor
        auto newEntityCopy = DomainType(newEntity_);
        modifiedEntity(DomainType(oldEntity), newEntityCopy);
    }

    virtual void deletedEntity(const ApplicationDomain::ApplicationDomainType &oldEntity) Q_DECL_OVERRIDE
    {
        deletedEntity(DomainType(oldEntity));
    }
};

} // namespace Sink
