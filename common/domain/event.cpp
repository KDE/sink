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

#include "../propertymapper.h"
#include "../typeindex.h"
#include "entity_generated.h"

#include "event_generated.h"

using namespace Sink::ApplicationDomain;

void TypeImplementation<Event>::configure(TypeIndex &index)
{
    index.addProperty<QByteArray>(Event::Uid::name);
}

void TypeImplementation<Event>::configure(ReadPropertyMapper<Buffer> &propertyMapper)
{
    propertyMapper.addMapping<Event::Summary, Buffer>(&Buffer::summary);
    propertyMapper.addMapping<Event::Description, Buffer>(&Buffer::description);
    propertyMapper.addMapping<Event::Uid, Buffer>(&Buffer::uid);
    propertyMapper.addMapping<Event::Attachment, Buffer>(&Buffer::attachment);
}

void TypeImplementation<Event>::configure(WritePropertyMapper<BufferBuilder> &propertyMapper)
{
    propertyMapper.addMapping<Event::Summary>(&BufferBuilder::add_summary);
    propertyMapper.addMapping<Event::Description>(&BufferBuilder::add_description);
    propertyMapper.addMapping<Event::Uid>(&BufferBuilder::add_uid);
    propertyMapper.addMapping<Event::Attachment>(&BufferBuilder::add_attachment);
}

void TypeImplementation<Event>::configure(IndexPropertyMapper &)
{

}
