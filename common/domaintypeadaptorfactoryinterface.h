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

namespace Akonadi2 {
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
    virtual ~DomainTypeAdaptorFactoryInterface() {};
    virtual QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor> createAdaptor(const Akonadi2::Entity &entity) = 0;
    virtual void createBuffer(const Akonadi2::ApplicationDomain::ApplicationDomainType &domainType, flatbuffers::FlatBufferBuilder &fbb, void const *metadataData = 0, size_t metadataSize = 0) = 0;
};
