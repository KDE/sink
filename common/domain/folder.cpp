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

#include <QByteArray>
#include <QString>

#include "../propertymapper.h"
#include "../typeindex.h"
#include "entitybuffer.h"
#include "entity_generated.h"

#include "folder_generated.h"

using namespace Sink::ApplicationDomain;

void TypeImplementation<Folder>::configure(TypeIndex &index)
{
    index.addProperty<QByteArray>(Folder::Parent::name);
    index.addProperty<QString>(Folder::Name::name);
}

void TypeImplementation<Folder>::configure(ReadPropertyMapper<Buffer> &propertyMapper)
{
    propertyMapper.addMapping<Folder::Parent, Buffer>(&Buffer::parent);
    propertyMapper.addMapping<Folder::Name, Buffer>(&Buffer::name);
    propertyMapper.addMapping<Folder::Icon, Buffer>(&Buffer::icon);
    propertyMapper.addMapping<Folder::SpecialPurpose, Buffer>(&Buffer::specialpurpose);
}

void TypeImplementation<Folder>::configure(WritePropertyMapper<BufferBuilder> &propertyMapper)
{
    propertyMapper.addMapping<Folder::Parent>(&BufferBuilder::add_parent);
    propertyMapper.addMapping<Folder::Name>(&BufferBuilder::add_name);
    propertyMapper.addMapping<Folder::Icon>(&BufferBuilder::add_icon);
    propertyMapper.addMapping<Folder::SpecialPurpose>(&BufferBuilder::add_specialpurpose);
}

void TypeImplementation<Folder>::configure(IndexPropertyMapper &)
{

}
