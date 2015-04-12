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

#include "domainadaptor.h"

template <>
flatbuffers::uoffset_t extractProperty<QString>(const QVariant &property, flatbuffers::FlatBufferBuilder &fbb)
{
    if (property.isValid()) {
        return fbb.CreateString(property.toString().toStdString()).o;
    }
    return 0;
}

template <>
QSharedPointer<ReadPropertyMapper<Akonadi2::ApplicationDomain::Buffer::Event> > initializeReadPropertyMapper<Akonadi2::ApplicationDomain::Buffer::Event>()
{
    auto propertyMapper = QSharedPointer<ReadPropertyMapper<Akonadi2::ApplicationDomain::Buffer::Event> >::create();
    propertyMapper->addMapping("summary", [](Akonadi2::ApplicationDomain::Buffer::Event const *buffer) -> QVariant {
        if (buffer->summary()) {
            return QString::fromStdString(buffer->summary()->c_str());
        }
        return QVariant();
    });
    propertyMapper->addMapping("uid", [](Akonadi2::ApplicationDomain::Buffer::Event const *buffer) -> QVariant {
        if (buffer->uid()) {
            return QString::fromStdString(buffer->uid()->c_str());
        }
        return QVariant();
    });
    return propertyMapper;
}

template <>
QSharedPointer<WritePropertyMapper<Akonadi2::ApplicationDomain::Buffer::EventBuilder> > initializeWritePropertyMapper<Akonadi2::ApplicationDomain::Buffer::EventBuilder>()
{
    auto propertyMapper = QSharedPointer<WritePropertyMapper<Akonadi2::ApplicationDomain::Buffer::EventBuilder> >::create();
    propertyMapper->addMapping("summary", [](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(Akonadi2::ApplicationDomain::Buffer::EventBuilder &)> {
        auto offset = extractProperty<QString>(value, fbb);
        return [offset](Akonadi2::ApplicationDomain::Buffer::EventBuilder &builder) { builder.add_summary(offset); };
    });
    propertyMapper->addMapping("uid", [](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(Akonadi2::ApplicationDomain::Buffer::EventBuilder &)> {
        auto offset = extractProperty<QString>(value, fbb);
        return [offset](Akonadi2::ApplicationDomain::Buffer::EventBuilder &builder) { builder.add_uid(offset); };
    });
    return propertyMapper;
}
