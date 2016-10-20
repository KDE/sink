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
#include "domain/event.h"
#include "domain/mail.h"
#include "domain/folder.h"
#include "bufferadaptor.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "entitybuffer.h"
#include "propertymapper.h"
#include "log.h"
#include "dummy_generated.h"

/**
 * Create a buffer from a domain object using the provided mappings
 */
template <class Builder, class Buffer>
flatbuffers::Offset<Buffer>
createBufferPart(const Sink::ApplicationDomain::ApplicationDomainType &domainObject, flatbuffers::FlatBufferBuilder &fbb, const WritePropertyMapper<Builder> &mapper)
{
    // First create a primitives such as strings using the mappings
    QList<std::function<void(Builder &)>> propertiesToAddToResource;
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
        propertyBuilder(builder);
    }
    return builder.Finish();
}

/**
 * Create the buffer and finish the FlatBufferBuilder.
 *
 * After this the buffer can be extracted from the FlatBufferBuilder object.
 */
template <typename Buffer, typename BufferBuilder>
static void createBufferPartBuffer(const Sink::ApplicationDomain::ApplicationDomainType &domainObject, flatbuffers::FlatBufferBuilder &fbb, WritePropertyMapper<BufferBuilder> &mapper)
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

/**
 * A generic adaptor implementation that uses a property mapper to read/write values.
 */
template <class LocalBuffer, class ResourceBuffer>
class DatastoreBufferAdaptor : public Sink::ApplicationDomain::BufferAdaptor
{
    SINK_DEBUG_AREA("bufferadaptor")
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
        if (mResourceBuffer && mResourceMapper->hasMapping(key)) {
            return mResourceMapper->getProperty(key, mResourceBuffer);
        } else if (mLocalBuffer && mLocalMapper->hasMapping(key)) {
            return mLocalMapper->getProperty(key, mLocalBuffer);
        }
        SinkWarning() << "No mapping available for key " << key << mLocalBuffer << mResourceBuffer;
        return QVariant();
    }

    /**
     * Returns all available properties for which a mapping exists (no matter what the buffer contains)
     */
    virtual QList<QByteArray> availableProperties() const Q_DECL_OVERRIDE
    {
        return mResourceMapper->availableProperties() + mLocalMapper->availableProperties();
    }

    LocalBuffer const *mLocalBuffer;
    ResourceBuffer const *mResourceBuffer;
    QSharedPointer<ReadPropertyMapper<LocalBuffer>> mLocalMapper;
    QSharedPointer<ReadPropertyMapper<ResourceBuffer>> mResourceMapper;
};

/**
 * The factory should define how to go from an entitybuffer (local + resource buffer), to a domain type adapter.
 * It defines how values are split accross local and resource buffer.
 * This is required by the facade the read the value, and by the pipeline preprocessors to access the domain values in a generic way.
 */
template <typename DomainType, typename ResourceBuffer = Sink::ApplicationDomain::Buffer::Dummy, typename ResourceBuilder = Sink::ApplicationDomain::Buffer::DummyBuilder>
class SINK_EXPORT DomainTypeAdaptorFactory : public DomainTypeAdaptorFactoryInterface
{
    typedef typename Sink::ApplicationDomain::TypeImplementation<DomainType>::Buffer LocalBuffer;
    typedef typename Sink::ApplicationDomain::TypeImplementation<DomainType>::BufferBuilder LocalBuilder;

public:
    DomainTypeAdaptorFactory()
        : mLocalMapper(QSharedPointer<ReadPropertyMapper<LocalBuffer>>::create()),
          mResourceMapper(QSharedPointer<ReadPropertyMapper<ResourceBuffer>>::create()),
          mLocalWriteMapper(QSharedPointer<WritePropertyMapper<LocalBuilder>>::create()),
          mResourceWriteMapper(QSharedPointer<WritePropertyMapper<ResourceBuilder>>::create())
    {
        Sink::ApplicationDomain::TypeImplementation<DomainType>::configure(*mLocalMapper);
        Sink::ApplicationDomain::TypeImplementation<DomainType>::configure(*mLocalWriteMapper);
    }
    virtual ~DomainTypeAdaptorFactory(){};

    /**
     * Creates an adaptor for the given domain and resource types.
     *
     * This returns by default a DatastoreBufferAdaptor initialized with the corresponding property mappers.
     */
    virtual QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> createAdaptor(const Sink::Entity &entity) Q_DECL_OVERRIDE
    {
        auto adaptor = QSharedPointer<DatastoreBufferAdaptor<LocalBuffer, ResourceBuffer>>::create();
        adaptor->mLocalBuffer = Sink::EntityBuffer::readBuffer<LocalBuffer>(entity.local());
        adaptor->mLocalMapper = mLocalMapper;
        adaptor->mResourceBuffer = Sink::EntityBuffer::readBuffer<ResourceBuffer>(entity.resource());
        adaptor->mResourceMapper = mResourceMapper;
        return adaptor;
    }

    virtual bool
    createBuffer(const Sink::ApplicationDomain::ApplicationDomainType &domainObject, flatbuffers::FlatBufferBuilder &fbb, void const *metadataData = 0, size_t metadataSize = 0) Q_DECL_OVERRIDE
    {
        flatbuffers::FlatBufferBuilder localFbb;
        if (mLocalWriteMapper) {
            // SinkTrace() << "Creating local buffer part";
            createBufferPartBuffer<LocalBuffer, LocalBuilder>(domainObject, localFbb, *mLocalWriteMapper);
        }

        flatbuffers::FlatBufferBuilder resFbb;
        if (mResourceWriteMapper) {
            // SinkTrace() << "Creating resouce buffer part";
            createBufferPartBuffer<ResourceBuffer, ResourceBuilder>(domainObject, resFbb, *mResourceWriteMapper);
        }

        Sink::EntityBuffer::assembleEntityBuffer(fbb, metadataData, metadataSize, resFbb.GetBufferPointer(), resFbb.GetSize(), localFbb.GetBufferPointer(), localFbb.GetSize());
        return true;
    }

    virtual bool createBuffer(const QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> &bufferAdaptor, flatbuffers::FlatBufferBuilder &fbb, void const *metadataData = 0, size_t metadataSize = 0) Q_DECL_OVERRIDE
    {
        //TODO rewrite the unterlying functions so we don't have to wrap the bufferAdaptor
        auto  newObject = Sink::ApplicationDomain::ApplicationDomainType("", "", 0, bufferAdaptor);
        //Serialize all properties
        newObject.setChangedProperties(bufferAdaptor->availableProperties().toSet());
        return createBuffer(newObject, fbb, metadataData, metadataSize);
    }


protected:
    QSharedPointer<ReadPropertyMapper<LocalBuffer>> mLocalMapper;
    QSharedPointer<ReadPropertyMapper<ResourceBuffer>> mResourceMapper;
    QSharedPointer<WritePropertyMapper<LocalBuilder>> mLocalWriteMapper;
    QSharedPointer<WritePropertyMapper<ResourceBuilder>> mResourceWriteMapper;
};
