/*
 *   Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "dummycalendar_generated.h"

using namespace DummyCalendar;
using namespace flatbuffers;

DummyEventAdaptorFactory::DummyEventAdaptorFactory()
    : DomainTypeAdaptorFactory()
{
    //TODO turn this into initializeReadPropertyMapper as well?
    mResourceMapper->addMapping("summary", [](DummyEvent const *buffer) -> QVariant {
        return propertyToVariant<QString>(buffer->summary());
    });
    mResourceMapper->addMapping("remoteId", [](DummyEvent const *buffer) -> QVariant {
        return propertyToVariant<QString>(buffer->remoteId());
    });

    mResourceWriteMapper->addMapping("summary", [](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(DummyEventBuilder &)> {
        auto offset = variantToProperty<QString>(value, fbb);
        return [offset](DummyEventBuilder &builder) { builder.add_summary(offset); };
    });
    mResourceWriteMapper->addMapping("remoteId", [](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(DummyEventBuilder &)> {
        auto offset = variantToProperty<QString>(value, fbb);
        return [offset](DummyEventBuilder &builder) { builder.add_remoteId(offset); };
    });
}

DummyMailAdaptorFactory::DummyMailAdaptorFactory()
    : DomainTypeAdaptorFactory()
{

}

