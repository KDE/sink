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

#include "entity_generated.h"
#include <QVariant>
#include <QByteArray>
#include <functional>
#include "clientapi.h" //for domain parts

#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "entitybuffer.h"

/**
 * The property mapper is a non-typesafe virtual dispatch.
 *
 * Instead of using an interface and requring each implementation to override
 * a virtual method per property, the property mapper can be filled with accessors
 * that extract the properties from resource types.
 */
template<typename BufferType>
class PropertyMapper
{
public:
    void setProperty(const QByteArray &key, const QVariant &value, BufferType *buffer)
    {
        if (mWriteAccessors.contains(key)) {
            auto accessor = mWriteAccessors.value(key);
            return accessor(value, buffer);
        }
    }

    virtual QVariant getProperty(const QByteArray &key, BufferType const *buffer) const
    {
        if (mReadAccessors.contains(key)) {
            auto accessor = mReadAccessors.value(key);
            return accessor(buffer);
        }
        return QVariant();
    }
    QHash<QByteArray, std::function<QVariant(BufferType const *)> > mReadAccessors;
    QHash<QByteArray, std::function<void(const QVariant &, BufferType*)> > mWriteAccessors;
};

/**
 * A generic adaptor implementation that uses a property mapper to read/write values.
 */
template <class LocalBuffer, class ResourceBuffer>
class GenericBufferAdaptor : public Akonadi2::ApplicationDomain::BufferAdaptor
{
public:
    GenericBufferAdaptor()
        : BufferAdaptor()
    {

    }

    void setProperty(const QByteArray &key, const QVariant &value)
    {
        if (mResourceMapper && mResourceMapper->mWriteAccessors.contains(key)) {
            // mResourceMapper->setProperty(key, value, mResourceBuffer);
        } else {
            // mLocalMapper.;
        }
    }

    virtual QVariant getProperty(const QByteArray &key) const
    {
        if (mResourceBuffer && mResourceMapper->mReadAccessors.contains(key)) {
            return mResourceMapper->getProperty(key, mResourceBuffer);
        } else if (mLocalBuffer && mLocalMapper->mReadAccessors.contains(key)) {
            return mLocalMapper->getProperty(key, mLocalBuffer);
        }
        qWarning() << "no mapping available for key " << key;
        return QVariant();
    }

    virtual QList<QByteArray> availableProperties() const
    {
        QList<QByteArray> props;
        props << mResourceMapper->mReadAccessors.keys();
        props << mLocalMapper->mReadAccessors.keys();
        return props;
    }

    LocalBuffer const *mLocalBuffer;
    ResourceBuffer const *mResourceBuffer;
    QSharedPointer<PropertyMapper<LocalBuffer> > mLocalMapper;
    QSharedPointer<PropertyMapper<ResourceBuffer> > mResourceMapper;
};

/**
 * Initializes the local property mapper.
 *
 * Provide an implementation for each application domain type.
 */
template <class T>
QSharedPointer<PropertyMapper<T> > initializePropertyMapper();

/**
 * The factory should define how to go from an entitybuffer (local + resource buffer), to a domain type adapter.
 * It defines how values are split accross local and resource buffer.
 * This is required by the facade the read the value, and by the pipeline preprocessors to access the domain values in a generic way.
 */
template<typename DomainType, typename LocalBuffer, typename ResourceBuffer>
class DomainTypeAdaptorFactory
{
public:
    DomainTypeAdaptorFactory() : mLocalMapper(initializePropertyMapper<LocalBuffer>()) {};
    virtual ~DomainTypeAdaptorFactory() {};

    /**
     * Creates an adaptor for the given domain and resource types.
     * 
     * This returns by default a GenericBufferAdaptor initialized with the corresponding property mappers.
     */
    virtual QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor> createAdaptor(const Akonadi2::Entity &entity)
    {
        const auto resourceBuffer = Akonadi2::EntityBuffer::readBuffer<ResourceBuffer>(entity.resource());
        const auto localBuffer = Akonadi2::EntityBuffer::readBuffer<LocalBuffer>(entity.local());
        // const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(entity.metadata());

        auto adaptor = QSharedPointer<GenericBufferAdaptor<LocalBuffer, ResourceBuffer> >::create();
        adaptor->mLocalBuffer = localBuffer;
        adaptor->mLocalMapper = mLocalMapper;
        adaptor->mResourceBuffer = resourceBuffer;
        adaptor->mResourceMapper = mResourceMapper;
        return adaptor;
    }

    virtual void createBuffer(const Akonadi2::ApplicationDomain::Event &event, flatbuffers::FlatBufferBuilder &fbb) {};

protected:
    QSharedPointer<PropertyMapper<LocalBuffer> > mLocalMapper;
    QSharedPointer<PropertyMapper<ResourceBuffer> > mResourceMapper;
};


