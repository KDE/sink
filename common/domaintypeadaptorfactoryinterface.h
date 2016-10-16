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

#include <QSharedPointer>

namespace Sink {
namespace ApplicationDomain {
class BufferAdaptor;
class ApplicationDomainType;
}
struct Entity;
}

namespace flatbuffers {
class FlatBufferBuilder;
}

class DomainTypeAdaptorFactoryInterface
{
public:
    typedef QSharedPointer<DomainTypeAdaptorFactoryInterface> Ptr;
    virtual ~DomainTypeAdaptorFactoryInterface(){};
    virtual QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> createAdaptor(const Sink::Entity &entity) = 0;

    /*
     * Creates a buffer from @param domainType
     *
     * Note that this only serialized parameters that are part of ApplicationDomainType::changedProperties()
     */
    virtual bool
    createBuffer(const Sink::ApplicationDomain::ApplicationDomainType &domainType, flatbuffers::FlatBufferBuilder &fbb, void const *metadataData = 0, size_t metadataSize = 0) = 0;
    virtual bool createBuffer(const QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> &bufferAdaptor, flatbuffers::FlatBufferBuilder &fbb, void const *metadataData = 0, size_t metadataSize = 0) = 0;
};
