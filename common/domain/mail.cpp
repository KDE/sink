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
#include "mail.h"

#include <QVector>
#include <QByteArray>
#include <QString>

#include "../propertymapper.h"
#include "../typeindex.h"
#include "entitybuffer.h"
#include "entity_generated.h"
#include "mail/threadindexer.h"
#include "domainadaptor.h"

#include "mail_generated.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

void TypeImplementation<Mail>::configure(TypeIndex &index)
{
    index.addProperty<Mail::Uid>();
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
    propertyMapper.addMapping<Mail::Uid, Buffer>(&Buffer::uid);
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
    propertyMapper.addMapping<Mail::Uid>(&BufferBuilder::add_uid);
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
