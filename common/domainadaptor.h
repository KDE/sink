/*
 *   Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "sink_export.h"
#include <QVariant>
#include <QByteArray>
#include <functional>

#include "domaintypeadaptorfactoryinterface.h"
#include "domain/applicationdomaintype.h"
#include "domain/typeimplementations.h"
#include "bufferadaptor.h"
#include "entity_generated.h"
#include "entitybuffer.h"
#include "propertymapper.h"
#include "log.h"

/**
 * Create a buffer from a domain object using the provided mappings
 */
template <class Builder, class Buffer>
flatbuffers::Offset<Buffer>
createBufferPart(const Sink::ApplicationDomain::ApplicationDomainType &domainObject, flatbuffers::FlatBufferBuilder &fbb, const PropertyMapper &mapper)
{
    // First create a primitives such as strings using the mappings
    QList<std::function<void(void *builder)>> propertiesToAddToResource;
    for (const auto &property : domainObject.changedProperties()) {
        // SinkTrace() << "copying property " << property;
        const auto value = domainObject.getProperty(property);
        if (mapper.hasMapping(property)) {
            mapper.setProperty(property, domainObject.getProperty(property), propertiesToAddToResource, fbb);
        } else {
            // SinkTrace() << "no mapping for property available " << property;
        }
    }

    // Then create all porperties using the above generated builderCalls
    Builder builder(fbb);
    for (auto propertyBuilder : propertiesToAddToResource) {
        propertyBuilder(&builder);
    }
    return builder.Finish();
}

/**
 * Create the buffer and finish the FlatBufferBuilder.
 *
 * After this the buffer can be extracted from the FlatBufferBuilder object.
 */
template <typename Buffer, typename BufferBuilder>
static void createBufferPartBuffer(const Sink::ApplicationDomain::ApplicationDomainType &domainObject, flatbuffers::FlatBufferBuilder &fbb, PropertyMapper &mapper)
{
    auto pos = createBufferPart<BufferBuilder, Buffer>(domainObject, fbb, mapper);
    // Because we cannot template the following call
    // Sink::ApplicationDomain::Buffer::FinishEventBuffer(fbb, pos);
    // FIXME: This means all buffers in here must have the AKFB identifier
    fbb.Finish(pos, "AKFB");
    flatbuffers::Verifier verifier(fbb.GetBufferPointer(), fbb.GetSize());
    if (!verifier.VerifyBuffer<Buffer>(nullptr)) {
        SinkWarning_(0, "bufferadaptor") << "Created invalid uffer";
    }
}

class IndexPropertyMapper
{
public:
    typedef std::function<QVariant(TypeIndex &index, const Sink::ApplicationDomain::BufferAdaptor &adaptor)> Accessor;
    virtual ~IndexPropertyMapper(){};

    virtual QVariant getProperty(const QByteArray &key, TypeIndex &index, const Sink::ApplicationDomain::BufferAdaptor &adaptor) const
    {
        auto accessor = mReadAccessors.value(key);
        Q_ASSERT(accessor);
        if (!accessor) {
            return QVariant();
        }
        return accessor(index, adaptor);
    }

    bool hasMapping(const QByteArray &key) const
    {
        return mReadAccessors.contains(key);
    }

    QList<QByteArray> availableProperties() const
    {
        return mReadAccessors.keys();
    }

    template<typename Property>
    void addIndexLookupProperty(const Accessor &accessor)
    {
        mReadAccessors.insert(Property::name, accessor);
    }

private:
    QHash<QByteArray, Accessor> mReadAccessors;
};

/**
 * A generic adaptor implementation that uses a property mapper to read/write values.
 */
class DatastoreBufferAdaptor : public Sink::ApplicationDomain::BufferAdaptor
{
public:
    DatastoreBufferAdaptor() : BufferAdaptor()
    {
    }

    virtual void setProperty(const QByteArray &key, const QVariant &value) Q_DECL_OVERRIDE
    {
        SinkWarning() << "Can't set property " << key;
        Q_ASSERT(false);
    }

    virtual QVariant getProperty(const QByteArray &key) const Q_DECL_OVERRIDE
    {
        if (mLocalBuffer && mLocalMapper->hasMapping(key)) {
            return mLocalMapper->getProperty(key, mLocalBuffer);
        } else if (mIndex && mIndexMapper->hasMapping(key)) {
            return mIndexMapper->getProperty(key, *mIndex, *this);
        }
        return QVariant();
    }

    /**
     * Returns all available properties for which a mapping exists (no matter what the buffer contains)
     */
    virtual QList<QByteArray> availableProperties() const Q_DECL_OVERRIDE
    {
        return mLocalMapper->availableProperties() + mIndexMapper->availableProperties();
    }

    void const *mLocalBuffer;
    QSharedPointer<PropertyMapper> mLocalMapper;
    QSharedPointer<IndexPropertyMapper> mIndexMapper;
    TypeIndex *mIndex;
};

/**
 * The factory should define how to go from an entitybuffer (local buffer), to a domain type adapter.
 * It defines how values are split accross local and resource buffer.
 * This is required by the facade the read the value, and by the pipeline preprocessors to access the domain values in a generic way.
 */
template <typename DomainType>
class SINK_EXPORT DomainTypeAdaptorFactory : public DomainTypeAdaptorFactoryInterface
{
    typedef typename Sink::ApplicationDomain::TypeImplementation<DomainType>::Buffer LocalBuffer;
    typedef typename Sink::ApplicationDomain::TypeImplementation<DomainType>::BufferBuilder LocalBuilder;

public:
    DomainTypeAdaptorFactory()
        : mPropertyMapper(QSharedPointer<PropertyMapper>::create()),
          mIndexMapper(QSharedPointer<IndexPropertyMapper>::create())
    {
        Sink::ApplicationDomain::TypeImplementation<DomainType>::configure(*mPropertyMapper);
        Sink::ApplicationDomain::TypeImplementation<DomainType>::configure(*mIndexMapper);
    }

    virtual ~DomainTypeAdaptorFactory(){};

    /**
     * Creates an adaptor for the given domain types.
     *
     * This returns by default a DatastoreBufferAdaptor initialized with the corresponding property mappers.
     */
    virtual QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> createAdaptor(const Sink::Entity &entity, TypeIndex *index = nullptr) Q_DECL_OVERRIDE
    {
        auto adaptor = QSharedPointer<DatastoreBufferAdaptor>::create();
        adaptor->mLocalBuffer = Sink::EntityBuffer::readBuffer<LocalBuffer>(entity.local());
        adaptor->mLocalMapper = mPropertyMapper;
        adaptor->mIndexMapper = mIndexMapper;
        adaptor->mIndex = index;
        return adaptor;
    }

    virtual bool
    createBuffer(const Sink::ApplicationDomain::ApplicationDomainType &domainObject, flatbuffers::FlatBufferBuilder &fbb, void const *metadataData = 0, size_t metadataSize = 0) Q_DECL_OVERRIDE
    {
        flatbuffers::FlatBufferBuilder localFbb;
        createBufferPartBuffer<LocalBuffer, LocalBuilder>(domainObject, localFbb, *mPropertyMapper);
        Sink::EntityBuffer::assembleEntityBuffer(fbb, metadataData, metadataSize, 0, 0, localFbb.GetBufferPointer(), localFbb.GetSize());
        return true;
    }

    virtual bool createBuffer(const QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> &bufferAdaptor, flatbuffers::FlatBufferBuilder &fbb, void const *metadataData = nullptr, size_t metadataSize = 0) Q_DECL_OVERRIDE
    {
        //TODO rewrite the unterlying functions so we don't have to wrap the bufferAdaptor
        auto  newObject = Sink::ApplicationDomain::ApplicationDomainType("", "", 0, bufferAdaptor);
        //Serialize all properties
        newObject.setChangedProperties(bufferAdaptor->availableProperties().toSet());
        return createBuffer(newObject, fbb, metadataData, metadataSize);
    }


protected:
    QSharedPointer<PropertyMapper> mPropertyMapper;
    QSharedPointer<IndexPropertyMapper> mIndexMapper;
};

/**
 * A default adaptorfactory implemenation that simply instantiates a generic resource
 */
template<typename DomainType>
class DefaultAdaptorFactory : public DomainTypeAdaptorFactory<DomainType>
{
public:
    DefaultAdaptorFactory() : DomainTypeAdaptorFactory<DomainType>()  {}
    virtual ~DefaultAdaptorFactory(){}
};

