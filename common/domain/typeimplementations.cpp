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
#include "mail/fulltextindexer.h"
#include "domainadaptor.h"
#include "typeimplementations_p.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

#define SINK_REGISTER_SERIALIZER(MAPPER, ENTITYTYPE, PROPERTY, LOWERCASEPROPERTY) \
    MAPPER.addMapping<ENTITYTYPE::PROPERTY, Sink::ApplicationDomain::Buffer::ENTITYTYPE, Sink::ApplicationDomain::Buffer::ENTITYTYPE##Builder>(&Sink::ApplicationDomain::Buffer::ENTITYTYPE::LOWERCASEPROPERTY, &Sink::ApplicationDomain::Buffer::ENTITYTYPE##Builder::add_##LOWERCASEPROPERTY);

typedef IndexConfig<Mail,
        SortedIndex<Mail::Date>,
        ValueIndex<Mail::Folder>,
        ValueIndex<Mail::ParentMessageId>,
        ValueIndex<Mail::MessageId>,
        ValueIndex<Mail::Draft>,
        SortedIndex<Mail::Folder, Mail::Date>,
        SecondaryIndex<Mail::MessageId, Mail::ThreadId>,
        SecondaryIndex<Mail::ThreadId, Mail::MessageId>,
        CustomSecondaryIndex<Mail::MessageId, Mail::ThreadId, ThreadIndexer>,
        CustomSecondaryIndex<Mail::Subject, Mail::Subject, FulltextIndexer>
    > MailIndexConfig;

typedef IndexConfig<Folder,
        ValueIndex<Folder::Name>,
        ValueIndex<Folder::Parent>
    > FolderIndexConfig;

typedef IndexConfig<Contact,
        ValueIndex<Contact::Uid>
    > ContactIndexConfig;

typedef IndexConfig<Addressbook,
        ValueIndex<Addressbook::Parent>
    > AddressbookIndexConfig;

typedef IndexConfig<Event,
        ValueIndex<Event::Uid>,
        SortedIndex<Event::StartTime>
    > EventIndexConfig;

typedef IndexConfig<Todo,
        ValueIndex<Todo::Uid>
    > TodoIndexConfig;

typedef IndexConfig<Calendar,
        ValueIndex<Calendar::Name>
    > CalendarIndexConfig;



void TypeImplementation<Mail>::configure(TypeIndex &index)
{
    MailIndexConfig::configure(index);
}

QMap<QByteArray, int> TypeImplementation<Mail>::typeDatabases()
{
    return merge(QMap<QByteArray, int>{{QByteArray{Mail::name} + ".main", 0}}, MailIndexConfig::databases());
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
    FolderIndexConfig::configure(index);
}

QMap<QByteArray, int> TypeImplementation<Folder>::typeDatabases()
{
    return merge(QMap<QByteArray, int>{{QByteArray{Folder::name} + ".main", 0}}, FolderIndexConfig::databases());
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
    ContactIndexConfig::configure(index);
}

QMap<QByteArray, int> TypeImplementation<Contact>::typeDatabases()
{
    return merge(QMap<QByteArray, int>{{QByteArray{Contact::name} + ".main", 0}}, ContactIndexConfig::databases());
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
    SINK_REGISTER_SERIALIZER(propertyMapper, Contact, Photo, photo);
}

void TypeImplementation<Contact>::configure(IndexPropertyMapper &)
{

}


void TypeImplementation<Addressbook>::configure(TypeIndex &index)
{
    AddressbookIndexConfig::configure(index);
}

QMap<QByteArray, int> TypeImplementation<Addressbook>::typeDatabases()
{
    return merge(QMap<QByteArray, int>{{QByteArray{Addressbook::name} + ".main", 0}}, AddressbookIndexConfig::databases());
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
    EventIndexConfig::configure(index);
}

QMap<QByteArray, int> TypeImplementation<Event>::typeDatabases()
{
    return merge(QMap<QByteArray, int>{{QByteArray{Event::name} + ".main", 0}}, EventIndexConfig::databases());
}

void TypeImplementation<Event>::configure(PropertyMapper &propertyMapper)
{
    SINK_REGISTER_SERIALIZER(propertyMapper, Event, Summary, summary);
    SINK_REGISTER_SERIALIZER(propertyMapper, Event, Description, description);
    SINK_REGISTER_SERIALIZER(propertyMapper, Event, Uid, uid);
    SINK_REGISTER_SERIALIZER(propertyMapper, Event, StartTime, startTime);
    SINK_REGISTER_SERIALIZER(propertyMapper, Event, EndTime, endTime);
    SINK_REGISTER_SERIALIZER(propertyMapper, Event, AllDay, allDay);
    SINK_REGISTER_SERIALIZER(propertyMapper, Event, Ical, ical);
    SINK_REGISTER_SERIALIZER(propertyMapper, Event, Calendar, calendar);
}

void TypeImplementation<Event>::configure(IndexPropertyMapper &)
{

}


void TypeImplementation<Todo>::configure(TypeIndex &index)
{
    TodoIndexConfig::configure(index);
}

QMap<QByteArray, int> TypeImplementation<Todo>::typeDatabases()
{
    return merge(QMap<QByteArray, int>{{QByteArray{Todo::name} + ".main", 0}}, TodoIndexConfig::databases());
}

void TypeImplementation<Todo>::configure(PropertyMapper &propertyMapper)
{
    SINK_REGISTER_SERIALIZER(propertyMapper, Todo, Uid, uid);
    SINK_REGISTER_SERIALIZER(propertyMapper, Todo, Summary, summary);
    SINK_REGISTER_SERIALIZER(propertyMapper, Todo, Description, description);
    SINK_REGISTER_SERIALIZER(propertyMapper, Todo, CompletedDate, completedDate);
    SINK_REGISTER_SERIALIZER(propertyMapper, Todo, DueDate, dueDate);
    SINK_REGISTER_SERIALIZER(propertyMapper, Todo, StartDate, startDate);
    SINK_REGISTER_SERIALIZER(propertyMapper, Todo, Status, status);
    SINK_REGISTER_SERIALIZER(propertyMapper, Todo, Priority, priority);
    SINK_REGISTER_SERIALIZER(propertyMapper, Todo, Categories, categories);
    SINK_REGISTER_SERIALIZER(propertyMapper, Todo, Ical, ical);
    SINK_REGISTER_SERIALIZER(propertyMapper, Todo, Calendar, calendar);
}

void TypeImplementation<Todo>::configure(IndexPropertyMapper &)
{

}


void TypeImplementation<Calendar>::configure(TypeIndex &index)
{
    CalendarIndexConfig::configure(index);
}

QMap<QByteArray, int> TypeImplementation<Calendar>::typeDatabases()
{
    return merge(QMap<QByteArray, int>{{QByteArray{Calendar::name} + ".main", 0}}, CalendarIndexConfig::databases());
}

void TypeImplementation<Calendar>::configure(PropertyMapper &propertyMapper)
{
    SINK_REGISTER_SERIALIZER(propertyMapper, Calendar, Name, name);
}

void TypeImplementation<Calendar>::configure(IndexPropertyMapper &) {}
