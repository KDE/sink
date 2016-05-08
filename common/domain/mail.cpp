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

#include "mail_generated.h"

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
    Trace() << "Indexing " << identifier;
    getIndex().add(identifier, bufferAdaptor, transaction);
}

void TypeImplementation<Mail>::removeIndex(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction)
{
    getIndex().remove(identifier, bufferAdaptor, transaction);
}

QSharedPointer<ReadPropertyMapper<TypeImplementation<Mail>::Buffer> > TypeImplementation<Mail>::initializeReadPropertyMapper()
{
    auto propertyMapper = QSharedPointer<ReadPropertyMapper<Buffer> >::create();
    propertyMapper->addMapping<QString, Buffer>("uid", &Buffer::uid);
    propertyMapper->addMapping<QString, Buffer>("sender", &Buffer::sender);
    propertyMapper->addMapping<QString, Buffer>("senderName", &Buffer::senderName);
    propertyMapper->addMapping<QString, Buffer>("subject", &Buffer::subject);
    propertyMapper->addMapping<QDateTime, Buffer>("date", &Buffer::date);
    propertyMapper->addMapping<bool, Buffer>("unread", &Buffer::unread);
    propertyMapper->addMapping<bool, Buffer>("important", &Buffer::important);
    propertyMapper->addMapping<QByteArray, Buffer>("folder", &Buffer::folder);
    propertyMapper->addMapping<QString, Buffer>("mimeMessage", &Buffer::mimeMessage);
    propertyMapper->addMapping<bool, Buffer>("draft", &Buffer::draft);
    return propertyMapper;
}

QSharedPointer<WritePropertyMapper<TypeImplementation<Mail>::BufferBuilder> > TypeImplementation<Mail>::initializeWritePropertyMapper()
{
    auto propertyMapper = QSharedPointer<WritePropertyMapper<BufferBuilder> >::create();
    propertyMapper->addMapping<QString>("uid", &BufferBuilder::add_uid);
    propertyMapper->addMapping<QString>("sender", &BufferBuilder::add_sender);
    propertyMapper->addMapping<QString>("senderName", &BufferBuilder::add_senderName);
    propertyMapper->addMapping<QString>("subject", &BufferBuilder::add_subject);
    propertyMapper->addMapping<QDateTime>("date", &BufferBuilder::add_date);
    propertyMapper->addMapping<bool>("unread", &BufferBuilder::add_unread);
    propertyMapper->addMapping<bool>("important", &BufferBuilder::add_important);
    propertyMapper->addMapping<QByteArray>("folder", &BufferBuilder::add_folder);
    propertyMapper->addMapping<QString>("mimeMessage", &BufferBuilder::add_mimeMessage);
    propertyMapper->addMapping<bool>("draft", &BufferBuilder::add_draft);
    return propertyMapper;
}
