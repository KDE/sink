/*
 *   Copyright (C) 2017 Christian Mollekopf <chrigi_1@fastaddressbook.fm>
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
#include "addressbook.h"

#include <QByteArray>
#include <QString>

#include "../propertymapper.h"
#include "../typeindex.h"
#include "entitybuffer.h"
#include "entity_generated.h"

#include "addressbook_generated.h"

using namespace Sink::ApplicationDomain;

void TypeImplementation<Addressbook>::configure(TypeIndex &index)
{
    index.addProperty<QByteArray>(Addressbook::Parent::name);
    index.addProperty<QString>(Addressbook::Name::name);
}

void TypeImplementation<Addressbook>::configure(ReadPropertyMapper<Buffer> &propertyMapper)
{
    propertyMapper.addMapping<Addressbook::Parent, Buffer>(&Buffer::parent);
    propertyMapper.addMapping<Addressbook::Name, Buffer>(&Buffer::name);
}

void TypeImplementation<Addressbook>::configure(WritePropertyMapper<BufferBuilder> &propertyMapper)
{
    propertyMapper.addMapping<Addressbook::Parent>(&BufferBuilder::add_parent);
    propertyMapper.addMapping<Addressbook::Name>(&BufferBuilder::add_name);
}

void TypeImplementation<Addressbook>::configure(IndexPropertyMapper &)
{

}
