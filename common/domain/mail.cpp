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
#include <QMutex>
#include <QMutexLocker>

#include "../resultset.h"
#include "../index.h"
#include "../storage.h"
#include "../log.h"
#include "../propertymapper.h"
#include "../query.h"
#include "../definitions.h"
#include "../typeindex.h"
#include "entitybuffer.h"
#include "entity_generated.h"

#include "mail_generated.h"

SINK_DEBUG_AREA("mail");

static QMutex sMutex;

using namespace Sink::ApplicationDomain;

static TypeIndex &getIndex()
{
    QMutexLocker locker(&sMutex);
    static TypeIndex *index = 0;
    if (!index) {
        index = new TypeIndex("mail");
        index->addProperty<QByteArray>("uid");
        index->addProperty<QByteArray>("sender");
        index->addProperty<QByteArray>("senderName");
        index->addProperty<QString>("subject");
        index->addProperty<QDateTime>("date");
        index->addProperty<QByteArray>("folder");
        index->addPropertyWithSorting<QByteArray, QDateTime>("folder", "date");
    }
    return *index;
}

ResultSet TypeImplementation<Mail>::queryIndexes(const Sink::Query &query, const QByteArray &resourceInstanceIdentifier, QSet<QByteArray> &appliedFilters, QByteArray &appliedSorting, Sink::Storage::Transaction &transaction)
{
    return getIndex().query(query, appliedFilters, appliedSorting, transaction);
}

void TypeImplementation<Mail>::index(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction)
{
    SinkTrace() << "Indexing " << identifier;
    getIndex().add(identifier, bufferAdaptor, transaction);
}

void TypeImplementation<Mail>::removeIndex(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction)
{
    getIndex().remove(identifier, bufferAdaptor, transaction);
}

QSharedPointer<ReadPropertyMapper<TypeImplementation<Mail>::Buffer> > TypeImplementation<Mail>::initializeReadPropertyMapper()
{
    auto propertyMapper = QSharedPointer<ReadPropertyMapper<Buffer> >::create();
    propertyMapper->addMapping<Mail::Uid, Buffer>(&Buffer::uid);
    propertyMapper->addMapping<Mail::Sender, Buffer>(&Buffer::sender);
    propertyMapper->addMapping<Mail::SenderName, Buffer>(&Buffer::senderName);
    propertyMapper->addMapping<Mail::Subject, Buffer>(&Buffer::subject);
    propertyMapper->addMapping<Mail::Date, Buffer>(&Buffer::date);
    propertyMapper->addMapping<Mail::Unread, Buffer>(&Buffer::unread);
    propertyMapper->addMapping<Mail::Important, Buffer>(&Buffer::important);
    propertyMapper->addMapping<Mail::Folder, Buffer>(&Buffer::folder);
    propertyMapper->addMapping<Mail::MimeMessage, Buffer>(&Buffer::mimeMessage);
    propertyMapper->addMapping<Mail::Draft, Buffer>(&Buffer::draft);
    propertyMapper->addMapping<Mail::Trash, Buffer>(&Buffer::trash);
    propertyMapper->addMapping<Mail::Sent, Buffer>(&Buffer::sent);
    return propertyMapper;
}

QSharedPointer<WritePropertyMapper<TypeImplementation<Mail>::BufferBuilder> > TypeImplementation<Mail>::initializeWritePropertyMapper()
{
    auto propertyMapper = QSharedPointer<WritePropertyMapper<BufferBuilder> >::create();

    propertyMapper->addMapping<Mail::Uid>(&BufferBuilder::add_uid);
    propertyMapper->addMapping<Mail::Sender>(&BufferBuilder::add_sender);
    propertyMapper->addMapping<Mail::SenderName>(&BufferBuilder::add_senderName);
    propertyMapper->addMapping<Mail::Subject>(&BufferBuilder::add_subject);
    propertyMapper->addMapping<Mail::Date>(&BufferBuilder::add_date);
    propertyMapper->addMapping<Mail::Unread>(&BufferBuilder::add_unread);
    propertyMapper->addMapping<Mail::Important>(&BufferBuilder::add_important);
    propertyMapper->addMapping<Mail::Folder>(&BufferBuilder::add_folder);
    propertyMapper->addMapping<Mail::MimeMessage>(&BufferBuilder::add_mimeMessage);
    propertyMapper->addMapping<Mail::Draft>(&BufferBuilder::add_draft);
    propertyMapper->addMapping<Mail::Trash>(&BufferBuilder::add_trash);
    propertyMapper->addMapping<Mail::Sent>(&BufferBuilder::add_sent);
    return propertyMapper;
}

DataStoreQuery TypeImplementation<Mail>::prepareQuery(const Sink::Query &query, Sink::Storage::Transaction &transaction)
{
    auto mapper = initializeReadPropertyMapper();
    return DataStoreQuery(query, ApplicationDomain::getTypeName<Mail>(), transaction, getIndex(), [mapper](const Sink::Entity &entity, const QByteArray &property) {

        const auto localBuffer = Sink::EntityBuffer::readBuffer<Buffer>(entity.local());
        return mapper->getProperty(property, localBuffer);
    });
}

