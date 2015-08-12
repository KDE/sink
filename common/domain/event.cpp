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
#include "event.h"

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

#include "event_generated.h"

using namespace Akonadi2::ApplicationDomain;

ResultSet TypeImplementation<Event>::queryIndexes(const Akonadi2::Query &query, const QByteArray &resourceInstanceIdentifier, QSet<QByteArray> &appliedFilters)
{
    QVector<QByteArray> keys;
    if (query.propertyFilter.contains("uid")) {
        Index uidIndex(Akonadi2::storageLocation(), resourceInstanceIdentifier + ".index.uid", Akonadi2::Storage::ReadOnly);
        uidIndex.lookup(query.propertyFilter.value("uid").toByteArray(), [&](const QByteArray &value) {
            keys << value;
        },
        [](const Index::Error &error) {
            Warning() << "Error in uid index: " <<  error.message;
        });
        appliedFilters << "uid";
    }
    return ResultSet(keys);
}

void TypeImplementation<Event>::index(const Event &type)
{
    Index uidIndex(Akonadi2::storageLocation(), type.resourceInstanceIdentifier() + ".index.uid", Akonadi2::Storage::ReadWrite);
    const auto uid = type.getProperty("uid");
    if (uid.isValid()) {
        uidIndex.add(uid.toByteArray(), type.identifier());
    }
}

QSharedPointer<ReadPropertyMapper<TypeImplementation<Event>::Buffer> > TypeImplementation<Event>::initializeReadPropertyMapper()
{
    auto propertyMapper = QSharedPointer<ReadPropertyMapper<Buffer> >::create();
    propertyMapper->addMapping("summary", [](Buffer const *buffer) -> QVariant {
        return propertyToVariant<QString>(buffer->summary());
    });
    propertyMapper->addMapping("uid", [](Buffer const *buffer) -> QVariant {
        return propertyToVariant<QString>(buffer->uid());
    });
    return propertyMapper;
}

QSharedPointer<WritePropertyMapper<TypeImplementation<Event>::BufferBuilder> > TypeImplementation<Event>::initializeWritePropertyMapper()
{
    auto propertyMapper = QSharedPointer<WritePropertyMapper<BufferBuilder> >::create();
    propertyMapper->addMapping("summary", [](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(BufferBuilder &)> {
        auto offset = variantToProperty<QString>(value, fbb);
        return [offset](BufferBuilder &builder) { builder.add_summary(offset); };
    });
    propertyMapper->addMapping("uid", [](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(BufferBuilder &)> {
        auto offset = variantToProperty<QString>(value, fbb);
        return [offset](BufferBuilder &builder) { builder.add_uid(offset); };
    });
    return propertyMapper;
}
