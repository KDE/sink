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

#include "../resultset.h"
#include "../index.h"
#include "../storage.h"
#include "../log.h"
#include "../propertymapper.h"
#include "../query.h"
#include "../definitions.h"

#include "mail_generated.h"

using namespace Akonadi2::ApplicationDomain;

ResultSet TypeImplementation<Mail>::queryIndexes(const Akonadi2::Query &query, const QByteArray &resourceInstanceIdentifier, QSet<QByteArray> &appliedFilters, Akonadi2::Storage::Transaction &transaction)
{
    QVector<QByteArray> keys;
    if (query.propertyFilter.contains("uid")) {
        Index uidIndex("mail.index.uid", transaction);
        uidIndex.lookup(query.propertyFilter.value("uid").toByteArray(), [&](const QByteArray &value) {
            keys << value;
        },
        [](const Index::Error &error) {
            Warning() << "Error in uid index: " <<  error.message;
        });
        appliedFilters << "uid";
    }
    return ResultSet(keys);
}

void TypeImplementation<Mail>::index(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Akonadi2::Storage::Transaction &transaction)
{
    const auto uid = bufferAdaptor.getProperty("uid");
    if (uid.isValid()) {
        Index("mail.index.uid", transaction).add(uid.toByteArray(), identifier);
    }
}

void TypeImplementation<Mail>::removeIndex(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Akonadi2::Storage::Transaction &transaction)
{
    const auto uid = bufferAdaptor.getProperty("uid");
    if (uid.isValid()) {
        Index("mail.index.uid", transaction).remove(uid.toByteArray(), identifier);
    }
}

QSharedPointer<ReadPropertyMapper<TypeImplementation<Mail>::Buffer> > TypeImplementation<Mail>::initializeReadPropertyMapper()
{
    auto propertyMapper = QSharedPointer<ReadPropertyMapper<Buffer> >::create();
    propertyMapper->addMapping<QString, Buffer>("uid", &Buffer::uid);
    propertyMapper->addMapping<QString, Buffer>("sender", &Buffer::sender);
    propertyMapper->addMapping<QString, Buffer>("senderName", &Buffer::senderName);
    propertyMapper->addMapping<QString, Buffer>("subject", &Buffer::subject);
    propertyMapper->addMapping<QString, Buffer>("date", &Buffer::date);
    propertyMapper->addMapping<bool, Buffer>("unread", &Buffer::unread);
    propertyMapper->addMapping<bool, Buffer>("important", &Buffer::important);
    propertyMapper->addMapping<QString, Buffer>("folder", &Buffer::folder);
    return propertyMapper;
}

QSharedPointer<WritePropertyMapper<TypeImplementation<Mail>::BufferBuilder> > TypeImplementation<Mail>::initializeWritePropertyMapper()
{
    auto propertyMapper = QSharedPointer<WritePropertyMapper<BufferBuilder> >::create();
    propertyMapper->addMapping<QString>("uid", &BufferBuilder::add_uid);
    propertyMapper->addMapping<QString>("sender", &BufferBuilder::add_sender);
    propertyMapper->addMapping<QString>("senderName", &BufferBuilder::add_senderName);
    propertyMapper->addMapping<QString>("subject", &BufferBuilder::add_subject);
    propertyMapper->addMapping<QString>("date", &BufferBuilder::add_date);
    // propertyMapper->addMapping<bool>("unread", &BufferBuilder::add_unread);
    // propertyMapper->addMapping<bool>("important", &BufferBuilder::add_important);
    propertyMapper->addMapping<QString>("folder", &BufferBuilder::add_folder);
    return propertyMapper;
}
