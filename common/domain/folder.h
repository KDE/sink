/*
 * Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "applicationdomaintype.h"

#include "storage.h"

class ResultSet;
class QByteArray;

template<typename T>
class ReadPropertyMapper;
template<typename T>
class WritePropertyMapper;

namespace Sink {
    class Query;

namespace ApplicationDomain {
    namespace Buffer {
        struct Folder;
        struct FolderBuilder;
    }

template<>
class TypeImplementation<Sink::ApplicationDomain::Folder> {
public:
    typedef Sink::ApplicationDomain::Buffer::Folder Buffer;
    typedef Sink::ApplicationDomain::Buffer::FolderBuilder BufferBuilder;
    static QSet<QByteArray> indexedProperties();
    static ResultSet queryIndexes(const Sink::Query &query, const QByteArray &resourceInstanceIdentifier, QSet<QByteArray> &appliedFilters, QByteArray &appliedSorting, Sink::Storage::Transaction &transaction);
    static void index(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction);
    static void removeIndex(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction);
    static QSharedPointer<ReadPropertyMapper<Buffer> > initializeReadPropertyMapper();
    static QSharedPointer<WritePropertyMapper<BufferBuilder> > initializeWritePropertyMapper();
};

}
}
