/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#pragma once

#include "common/clientapi.h"
#include "common/storage.h"
#include "entity_generated.h"
#include "event_generated.h"
#include "dummycalendar_generated.h"

namespace Akonadi2 {
    class ResourceAccess;
}

/**
 * The property mapper holds accessor functions for all properties.
 *
 * It is by default initialized with accessors that access the local-only buffer,
 * and resource simply have to overwrite those accessors.
 */
template<typename BufferType>
class PropertyMapper
{
public:
    void setProperty(const QString &key, const QVariant &value, BufferType *buffer)
    {
        if (mWriteAccessors.contains(key)) {
            auto accessor = mWriteAccessors.value(key);
            return accessor(value, buffer);
        }
    }

    virtual QVariant getProperty(const QString &key, BufferType const *buffer) const
    {
        if (mReadAccessors.contains(key)) {
            auto accessor = mReadAccessors.value(key);
            return accessor(buffer);
        }
        return QVariant();
    }
    QHash<QString, std::function<QVariant(BufferType const *)> > mReadAccessors;
    QHash<QString, std::function<void(const QVariant &, BufferType*)> > mWriteAccessors;
};

//The factory should define how to go from an entitybuffer (local + resource buffer), to a domain type adapter.
//It defines how values are split accross local and resource buffer.
//This is required by the facade the read the value, and by the pipeline preprocessors to access the domain values in a generic way.
template<typename DomainType, typename LocalBuffer, typename ResourceBuffer>
class DomainTypeAdaptorFactory
{
};

template<typename LocalBuffer, typename ResourceBuffer>
class DomainTypeAdaptorFactory<typename Akonadi2::Domain::Event, LocalBuffer, ResourceBuffer>
{
public:
    QSharedPointer<Akonadi2::Domain::BufferAdaptor> createAdaptor(const Akonadi2::Entity &entity);

// private:
    QSharedPointer<PropertyMapper<LocalBuffer> > mLocalMapper;
    QSharedPointer<PropertyMapper<ResourceBuffer> > mResourceMapper;
};

class DummyResourceFacade : public Akonadi2::StoreFacade<Akonadi2::Domain::Event>
{
public:
    DummyResourceFacade();
    virtual ~DummyResourceFacade();
    virtual void create(const Akonadi2::Domain::Event &domainObject);
    virtual void modify(const Akonadi2::Domain::Event &domainObject);
    virtual void remove(const Akonadi2::Domain::Event &domainObject);
    virtual void load(const Akonadi2::Query &query, const std::function<void(const Akonadi2::Domain::Event::Ptr &)> &resultCallback, const std::function<void()> &completeCallback);

private:
    void synchronizeResource(const std::function<void()> &continuation);
    QSharedPointer<Akonadi2::ResourceAccess> mResourceAccess;
    QSharedPointer<DomainTypeAdaptorFactory<Akonadi2::Domain::Event, Akonadi2::Domain::Buffer::Event, DummyCalendar::DummyEvent> > mFactory;
};
