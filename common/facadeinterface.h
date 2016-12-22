/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include <Async/Async>
#include <QByteArray>
#include <QSharedPointer>
#include <QPair>
#include "applicationdomaintype.h"
#include "resultprovider.h"

namespace Sink {
class Query;
namespace Log {
    struct Context;
}

/**
 * Interface for the store facade.
 *
 * All methods are synchronous.
 * Facades are stateful (they hold connections to resources and database).
 *
 * TODO: would it make sense to split the write, read and notification parts? (we could potentially save some connections)
 */
template <class DomainType>
class StoreFacade
{
public:
    virtual ~StoreFacade(){};
    QByteArray type() const
    {
        return ApplicationDomain::getTypeName<DomainType>();
    }

    /**
     * Create an entity in the store.
     *
     * The job returns succefully once the task has been successfully placed in the queue
     */
    virtual KAsync::Job<void> create(const DomainType &domainObject) = 0;

    /**
     * Modify an entity in the store.
     *
     * The job returns succefully once the task has been successfully placed in the queue
     */
    virtual KAsync::Job<void> modify(const DomainType &domainObject) = 0;

    /**
     * Move an entity to a new resource.
     *
     * The job returns succefully once the task has been successfully placed in the queue
     */
    virtual KAsync::Job<void> move(const DomainType &domainObject, const QByteArray &newResource) = 0;

    /**
     * Copy an entity to a new resource.
     *
     * The job returns succefully once the task has been successfully placed in the queue
     */
    virtual KAsync::Job<void> copy(const DomainType &domainObject, const QByteArray &newResource) = 0;

    /**
     * Remove an entity from the store.
     *
     * The job returns succefully once the task has been successfully placed in the queue
     */
    virtual KAsync::Job<void> remove(const DomainType &domainObject) = 0;

    /**
     * Load entities from the store.
     */
    virtual QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename DomainType::Ptr>::Ptr> load(const Query &query, const Log::Context &) = 0;
};

template <class DomainType>
class NullFacade : public StoreFacade<DomainType>
{
public:
    virtual ~NullFacade(){};
    KAsync::Job<void> create(const DomainType &domainObject)
    {
        return KAsync::error<void>(-1, "Failed to create a facade");
    }

    KAsync::Job<void> modify(const DomainType &domainObject)
    {
        return KAsync::error<void>(-1, "Failed to create a facade");
    }

    KAsync::Job<void> move(const DomainType &domainObject, const QByteArray &newResource)
    {
        return KAsync::error<void>(-1, "Failed to create a facade");
    }

    KAsync::Job<void> copy(const DomainType &domainObject, const QByteArray &newResource)
    {
        return KAsync::error<void>(-1, "Failed to create a facade");
    }

    KAsync::Job<void> remove(const DomainType &domainObject)
    {
        return KAsync::error<void>(-1, "Failed to create a facade");
    }

    QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename DomainType::Ptr>::Ptr> load(const Query &query, const Log::Context &)
    {
        return qMakePair(KAsync::null<void>(), typename Sink::ResultEmitter<typename DomainType::Ptr>::Ptr());
    }
};
}
