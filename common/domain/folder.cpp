/*
 *   Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastfolder.fm>
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
#include "folder.h"

#include <QVector>
#include <QByteArray>
#include <QString>

#include "../resultset.h"
#include "../index.h"
#include "../storage.h"
#include "../log.h"
#include "../propertymapper.h"
#include "../query.h"
#include "../definitions.h"

#include "folder_generated.h"

using namespace Akonadi2::ApplicationDomain;

ResultSet TypeImplementation<Folder>::queryIndexes(const Akonadi2::Query &query, const QByteArray &resourceInstanceIdentifier, QSet<QByteArray> &appliedFilters, Akonadi2::Storage::Transaction &transaction)
{
    QVector<QByteArray> keys;
    if (query.propertyFilter.contains("parent")) {
        Index index("folder.index.parent", transaction);
        auto lookupKey = query.propertyFilter.value("parent").toByteArray();
        if (lookupKey.isEmpty()) {
            lookupKey = "toplevel";
        }
        index.lookup(lookupKey, [&](const QByteArray &value) {
            keys << value;
        },
        [](const Index::Error &error) {
            Warning() << "Error in uid index: " <<  error.message;
        });
        appliedFilters << "parent";
    }
    Trace() << "Index lookup found " << keys.size() << " keys.";
    return ResultSet(keys);
}

void TypeImplementation<Folder>::index(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Akonadi2::Storage::Transaction &transaction)
{
    const auto parent = bufferAdaptor.getProperty("parent");
    Trace() << "indexing " << identifier << " with parent " << parent.toByteArray();
    if (parent.isValid()) {
        Index("folder.index.parent", transaction).add(parent.toByteArray(), identifier);
    } else {
        Index("folder.index.parent", transaction).add("toplevel", identifier);
    }
}

void TypeImplementation<Folder>::removeIndex(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Akonadi2::Storage::Transaction &transaction)
{
    const auto parent = bufferAdaptor.getProperty("parent");
    if (parent.isValid()) {
        Index("folder.index.parent", transaction).remove(parent.toByteArray(), identifier);
    } else {
        Index("folder.index.parent", transaction).remove("toplevel", identifier);
    }
}

QSharedPointer<ReadPropertyMapper<TypeImplementation<Folder>::Buffer> > TypeImplementation<Folder>::initializeReadPropertyMapper()
{
    auto propertyMapper = QSharedPointer<ReadPropertyMapper<Buffer> >::create();
    propertyMapper->addMapping<QString, Buffer>("parent", &Buffer::parent);
    propertyMapper->addMapping<QString, Buffer>("name", &Buffer::name);
    return propertyMapper;
}

QSharedPointer<WritePropertyMapper<TypeImplementation<Folder>::BufferBuilder> > TypeImplementation<Folder>::initializeWritePropertyMapper()
{
    auto propertyMapper = QSharedPointer<WritePropertyMapper<BufferBuilder> >::create();
    propertyMapper->addMapping("parent", [](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(BufferBuilder &)> {
        auto offset = variantToProperty<QString>(value, fbb);
        return [offset](BufferBuilder &builder) { builder.add_parent(offset); };
    });
    propertyMapper->addMapping("name", [](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(BufferBuilder &)> {
        auto offset = variantToProperty<QString>(value, fbb);
        return [offset](BufferBuilder &builder) { builder.add_name(offset); };
    });
    return propertyMapper;
}
