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
#include "typeimplementations.h"

#include <QVector>
#include <QByteArray>
#include <QString>

#include "../propertymapper.h"
#include "../typeindex.h"
#include "entitybuffer.h"
#include "entity_generated.h"
#include "mail/threadindexer.h"
#include "domainadaptor.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

#define SINK_REGISTER_SERIALIZER(MAPPER, ENTITYTYPE, PROPERTY, LOWERCASEPROPERTY) \
    MAPPER.addMapping<ENTITYTYPE::PROPERTY, Sink::ApplicationDomain::Buffer::ENTITYTYPE, Sink::ApplicationDomain::Buffer::ENTITYTYPE##Builder>(&Sink::ApplicationDomain::Buffer::ENTITYTYPE::LOWERCASEPROPERTY, &Sink::ApplicationDomain::Buffer::ENTITYTYPE##Builder::add_##LOWERCASEPROPERTY);


void TypeImplementation<Mail>::configure(TypeIndex &index)
{
    // index.addProperty<Mail::Sender>();
    /* index.addProperty<QByteArray>(Mail::SenderName::name); */
    /* index->addProperty<QString>(Mail::Subject::name); */
    /* index->addFulltextProperty<QString>(Mail::Subject::name); */
    index.addProperty<Mail::Date>();
    index.addProperty<Mail::Folder>();
    index.addPropertyWithSorting<Mail::Folder, Mail::Date>();
    index.addProperty<Mail::ParentMessageId>();
    index.addProperty<Mail::MessageId>();

    index.addSecondaryPropertyIndexer<Mail::MessageId, Mail::ThreadId, ThreadIndexer>();
    index.addSecondaryProperty<Mail::MessageId, Mail::ThreadId>();
    index.addSecondaryProperty<Mail::ThreadId, Mail::MessageId>();
}

void TypeImplementation<Mail>::configure(IndexPropertyMapper &indexPropertyMapper)
{
    indexPropertyMapper.addIndexLookupProperty<Mail::ThreadId>([](TypeIndex &index, const ApplicationDomain::BufferAdaptor &entity) {
            auto messageId = entity.getProperty(Mail::MessageId::name);
            auto thread = index.secondaryLookup<Mail::MessageId, Mail::ThreadId>(messageId);
            if (!thread.isEmpty()) {
                return thread.first();
            }
            return QByteArray{};
        });
}

void TypeImplementation<Mail>::configure(PropertyMapper &propertyMapper)
{
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, Sender, sender);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, To, to);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, Cc, cc);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, Bcc, bcc);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, Subject, subject);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, Date, date);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, Unread, unread);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, Important, important);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, Folder, folder);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, MimeMessage, mimeMessage);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, FullPayloadAvailable, fullPayloadAvailable);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, Draft, draft);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, Trash, trash);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, Sent, sent);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, MessageId, messageId);
    SINK_REGISTER_SERIALIZER(propertyMapper, Mail, ParentMessageId, parentMessageId);
}

void TypeImplementation<Folder>::configure(TypeIndex &index)
{
    index.addProperty<QByteArray>(Folder::Parent::name);
    index.addProperty<QString>(Folder::Name::name);
}

void TypeImplementation<Folder>::configure(PropertyMapper &propertyMapper)
{
    SINK_REGISTER_SERIALIZER(propertyMapper, Folder, Parent, parent);
    SINK_REGISTER_SERIALIZER(propertyMapper, Folder, Name, name);
    SINK_REGISTER_SERIALIZER(propertyMapper, Folder, Icon, icon);
    SINK_REGISTER_SERIALIZER(propertyMapper, Folder, SpecialPurpose, specialpurpose);
    SINK_REGISTER_SERIALIZER(propertyMapper, Folder, Enabled, enabled);
}

void TypeImplementation<Folder>::configure(IndexPropertyMapper &)
{

}


void TypeImplementation<Contact>::configure(TypeIndex &index)
{
    index.addProperty<QByteArray>(Contact::Uid::name);
}

void TypeImplementation<Contact>::configure(PropertyMapper &propertyMapper)
{
    SINK_REGISTER_SERIALIZER(propertyMapper, Contact, Uid, uid);
    SINK_REGISTER_SERIALIZER(propertyMapper, Contact, Fn, fn);
    SINK_REGISTER_SERIALIZER(propertyMapper, Contact, Emails, emails);
    SINK_REGISTER_SERIALIZER(propertyMapper, Contact, Vcard, vcard);
    SINK_REGISTER_SERIALIZER(propertyMapper, Contact, Addressbook, addressbook);
    SINK_REGISTER_SERIALIZER(propertyMapper, Contact, Firstname, firstname);
    SINK_REGISTER_SERIALIZER(propertyMapper, Contact, Lastname, lastname);
}

void TypeImplementation<Contact>::configure(IndexPropertyMapper &)
{

}


void TypeImplementation<Addressbook>::configure(TypeIndex &index)
{
    index.addProperty<QByteArray>(Addressbook::Parent::name);
    index.addProperty<QString>(Addressbook::Name::name);
}

void TypeImplementation<Addressbook>::configure(PropertyMapper &propertyMapper)
{
    SINK_REGISTER_SERIALIZER(propertyMapper, Addressbook, Parent, parent);
    SINK_REGISTER_SERIALIZER(propertyMapper, Addressbook, Name, name);
}

void TypeImplementation<Addressbook>::configure(IndexPropertyMapper &)
{

}


void TypeImplementation<Event>::configure(TypeIndex &index)
{
    index.addProperty<QByteArray>(Event::Uid::name);
}

void TypeImplementation<Event>::configure(PropertyMapper &propertyMapper)
{
    SINK_REGISTER_SERIALIZER(propertyMapper, Event, Summary, summary);
    SINK_REGISTER_SERIALIZER(propertyMapper, Event, Description, description);
    SINK_REGISTER_SERIALIZER(propertyMapper, Event, Uid, uid);
    SINK_REGISTER_SERIALIZER(propertyMapper, Event, Attachment, attachment);
}

void TypeImplementation<Event>::configure(IndexPropertyMapper &)
{

}
