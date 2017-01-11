/*
 * Copyright (C) 2017 Sandro Knauß <knauss@kolabsys.com>
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
#include "contact.h"

#include <QVector>
#include <QByteArray>
#include <QString>

#include "../propertymapper.h"
#include "../typeindex.h"
#include "entity_generated.h"

#include "contact_generated.h"

using namespace Sink::ApplicationDomain;

void TypeImplementation<Contact>::configure(TypeIndex &index)
{
    index.addProperty<QByteArray>(Contact::Uid::name);
}

void TypeImplementation<Contact>::configure(ReadPropertyMapper<Buffer> &propertyMapper)
{
    propertyMapper.addMapping<Contact::Uid, Buffer>(&Buffer::uid);
    propertyMapper.addMapping<Contact::Fn, Buffer>(&Buffer::fn);
    propertyMapper.addMapping<Contact::Emails, Buffer>(&Buffer::emails);
    propertyMapper.addMapping<Contact::Vcard, Buffer>(&Buffer::vcard);
}

void TypeImplementation<Contact>::configure(WritePropertyMapper<BufferBuilder> &propertyMapper)
{
    propertyMapper.addMapping<Contact::Uid>(&BufferBuilder::add_uid);
    propertyMapper.addMapping<Contact::Fn>(&BufferBuilder::add_fn);
    propertyMapper.addMapping<Contact::Emails>(&BufferBuilder::add_emails);
    propertyMapper.addMapping<Contact::Vcard>(&BufferBuilder::add_vcard);
}

void TypeImplementation<Contact>::configure(IndexPropertyMapper &)
{

}
