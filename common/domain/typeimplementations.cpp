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

void TypeImplementation<Mail>::configure(ReadPropertyMapper<Buffer> &propertyMapper)
{
    propertyMapper.addMapping<Mail::Sender, Buffer>(&Buffer::sender);
    propertyMapper.addMapping<Mail::To, Buffer>(&Buffer::to);
    propertyMapper.addMapping<Mail::Cc, Buffer>(&Buffer::cc);
    propertyMapper.addMapping<Mail::Bcc, Buffer>(&Buffer::bcc);
    propertyMapper.addMapping<Mail::Subject, Buffer>(&Buffer::subject);
    propertyMapper.addMapping<Mail::Date, Buffer>(&Buffer::date);
    propertyMapper.addMapping<Mail::Unread, Buffer>(&Buffer::unread);
    propertyMapper.addMapping<Mail::Important, Buffer>(&Buffer::important);
    propertyMapper.addMapping<Mail::Folder, Buffer>(&Buffer::folder);
    propertyMapper.addMapping<Mail::MimeMessage, Buffer>(&Buffer::mimeMessage);
    propertyMapper.addMapping<Mail::FullPayloadAvailable, Buffer>(&Buffer::fullPayloadAvailable);
    propertyMapper.addMapping<Mail::Draft, Buffer>(&Buffer::draft);
    propertyMapper.addMapping<Mail::Trash, Buffer>(&Buffer::trash);
    propertyMapper.addMapping<Mail::Sent, Buffer>(&Buffer::sent);
    propertyMapper.addMapping<Mail::MessageId, Buffer>(&Buffer::messageId);
    propertyMapper.addMapping<Mail::ParentMessageId, Buffer>(&Buffer::parentMessageId);
}

void TypeImplementation<Mail>::configure(WritePropertyMapper<BufferBuilder> &propertyMapper)
{
    propertyMapper.addMapping<Mail::Sender>(&BufferBuilder::add_sender);
    propertyMapper.addMapping<Mail::To>(&BufferBuilder::add_to);
    propertyMapper.addMapping<Mail::Cc>(&BufferBuilder::add_cc);
    propertyMapper.addMapping<Mail::Bcc>(&BufferBuilder::add_bcc);
    propertyMapper.addMapping<Mail::Subject>(&BufferBuilder::add_subject);
    propertyMapper.addMapping<Mail::Date>(&BufferBuilder::add_date);
    propertyMapper.addMapping<Mail::Unread>(&BufferBuilder::add_unread);
    propertyMapper.addMapping<Mail::Important>(&BufferBuilder::add_important);
    propertyMapper.addMapping<Mail::Folder>(&BufferBuilder::add_folder);
    propertyMapper.addMapping<Mail::MimeMessage>(&BufferBuilder::add_mimeMessage);
    propertyMapper.addMapping<Mail::FullPayloadAvailable>(&BufferBuilder::add_fullPayloadAvailable);
    propertyMapper.addMapping<Mail::Draft>(&BufferBuilder::add_draft);
    propertyMapper.addMapping<Mail::Trash>(&BufferBuilder::add_trash);
    propertyMapper.addMapping<Mail::Sent>(&BufferBuilder::add_sent);
    propertyMapper.addMapping<Mail::MessageId>(&BufferBuilder::add_messageId);
    propertyMapper.addMapping<Mail::ParentMessageId>(&BufferBuilder::add_parentMessageId);
}


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
    propertyMapper.addMapping<Folder::Enabled, Buffer>(&Buffer::enabled);
}

void TypeImplementation<Folder>::configure(WritePropertyMapper<BufferBuilder> &propertyMapper)
{
    propertyMapper.addMapping<Folder::Parent>(&BufferBuilder::add_parent);
    propertyMapper.addMapping<Folder::Name>(&BufferBuilder::add_name);
    propertyMapper.addMapping<Folder::Icon>(&BufferBuilder::add_icon);
    propertyMapper.addMapping<Folder::SpecialPurpose>(&BufferBuilder::add_specialpurpose);
    propertyMapper.addMapping<Folder::Enabled>(&BufferBuilder::add_enabled);
}

void TypeImplementation<Folder>::configure(IndexPropertyMapper &)
{

}


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
    propertyMapper.addMapping<Contact::Addressbook, Buffer>(&Buffer::addressbook);
}

void TypeImplementation<Contact>::configure(WritePropertyMapper<BufferBuilder> &propertyMapper)
{
    propertyMapper.addMapping<Contact::Uid>(&BufferBuilder::add_uid);
    propertyMapper.addMapping<Contact::Fn>(&BufferBuilder::add_fn);
    propertyMapper.addMapping<Contact::Emails>(&BufferBuilder::add_emails);
    propertyMapper.addMapping<Contact::Vcard>(&BufferBuilder::add_vcard);
    propertyMapper.addMapping<Contact::Addressbook>(&BufferBuilder::add_addressbook);
}

void TypeImplementation<Contact>::configure(IndexPropertyMapper &)
{

}


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
