/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include "event.h"

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
#include "../typeindex.h"

#include "event_generated.h"

using namespace Akonadi2::ApplicationDomain;

static TypeIndex &getIndex()
{
    static TypeIndex *index = 0;
    if (!index) {
        index = new TypeIndex("event");
        index->addProperty<QByteArray>("uid");
    }
    return *index;
}

ResultSet TypeImplementation<Event>::queryIndexes(const Akonadi2::Query &query, const QByteArray &resourceInstanceIdentifier, QSet<QByteArray> &appliedFilters, Akonadi2::Storage::Transaction &transaction)
{
    return getIndex().query(query, appliedFilters, transaction);
}

void TypeImplementation<Event>::index(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Akonadi2::Storage::Transaction &transaction)
{
    return getIndex().add(identifier, bufferAdaptor, transaction);
}

void TypeImplementation<Event>::removeIndex(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Akonadi2::Storage::Transaction &transaction)
{
    return getIndex().remove(identifier, bufferAdaptor, transaction);
}

QSharedPointer<ReadPropertyMapper<TypeImplementation<Event>::Buffer> > TypeImplementation<Event>::initializeReadPropertyMapper()
{
    auto propertyMapper = QSharedPointer<ReadPropertyMapper<Buffer> >::create();
    propertyMapper->addMapping<QString, Buffer>("summary", &Buffer::summary);
    propertyMapper->addMapping<QString, Buffer>("uid", &Buffer::uid);
    return propertyMapper;
}

QSharedPointer<WritePropertyMapper<TypeImplementation<Event>::BufferBuilder> > TypeImplementation<Event>::initializeWritePropertyMapper()
{
    auto propertyMapper = QSharedPointer<WritePropertyMapper<BufferBuilder> >::create();
    propertyMapper->addMapping<QString>("summary", &BufferBuilder::add_summary);
    propertyMapper->addMapping<QString>("uid", &BufferBuilder::add_uid);
    return propertyMapper;
}
