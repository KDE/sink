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

#include "../clientapi.h"

class ResultSet;
class QByteArray;

template<typename T>
class ReadPropertyMapper;
template<typename T>
class WritePropertyMapper;

namespace Akonadi2 {
    class Query;

namespace ApplicationDomain {
    namespace Buffer {
        class Event;
        class EventBuilder;
    }

/**
 * Implements all type-specific code such as updating and querying indexes.
 * 
 * These are type specifiy default implementations. Theoretically a resource could implement it's own implementation.
 */
template<>
class TypeImplementation<Akonadi2::ApplicationDomain::Event> {
public:
    typedef Akonadi2::ApplicationDomain::Buffer::Event Buffer;
    typedef Akonadi2::ApplicationDomain::Buffer::EventBuilder BufferBuilder;
    static QSet<QByteArray> indexedProperties();
    /**
     * Returns the potential result set based on the indexes.
     * 
     * An empty result set indicates that a full scan is required.
     */
    static ResultSet queryIndexes(const Akonadi2::Query &query, const QByteArray &resourceInstanceIdentifier, QSet<QByteArray> &appliedFilters);
    static void index(const Event &type);
    static QSharedPointer<ReadPropertyMapper<Buffer> > initializeReadPropertyMapper();
    static QSharedPointer<WritePropertyMapper<BufferBuilder> > initializeWritePropertyMapper();
};

}
}
