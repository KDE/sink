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

#include "folder_generated.h"

static QMutex sMutex;

using namespace Sink::ApplicationDomain;

static TypeIndex &getIndex()
{
    QMutexLocker locker(&sMutex);
    static TypeIndex *index = 0;
    if (!index) {
        index = new TypeIndex("folder");
        index->addProperty<QByteArray>("parent");
        index->addProperty<QString>("name");
    }
    return *index;
}

ResultSet TypeImplementation<Folder>::queryIndexes(const Sink::Query &query, const QByteArray &resourceInstanceIdentifier, QSet<QByteArray> &appliedFilters, Sink::Storage::Transaction &transaction)
{
    return getIndex().query(query, appliedFilters, transaction);
}

void TypeImplementation<Folder>::index(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction)
{
    Trace() << "Indexing " << identifier;
    getIndex().add(identifier, bufferAdaptor, transaction);
}

void TypeImplementation<Folder>::removeIndex(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction)
{
    getIndex().remove(identifier, bufferAdaptor, transaction);
}

QSharedPointer<ReadPropertyMapper<TypeImplementation<Folder>::Buffer> > TypeImplementation<Folder>::initializeReadPropertyMapper()
{
    auto propertyMapper = QSharedPointer<ReadPropertyMapper<Buffer> >::create();
    propertyMapper->addMapping<QByteArray, Buffer>("parent", &Buffer::parent);
    propertyMapper->addMapping<QString, Buffer>("name", &Buffer::name);
    propertyMapper->addMapping<QByteArray, Buffer>("icon", &Buffer::icon);
    return propertyMapper;
}

QSharedPointer<WritePropertyMapper<TypeImplementation<Folder>::BufferBuilder> > TypeImplementation<Folder>::initializeWritePropertyMapper()
{
    auto propertyMapper = QSharedPointer<WritePropertyMapper<BufferBuilder> >::create();
    propertyMapper->addMapping<QByteArray>("parent", &BufferBuilder::add_parent);
    propertyMapper->addMapping<QString>("name", &BufferBuilder::add_name);
    propertyMapper->addMapping<QByteArray>("icon", &BufferBuilder::add_icon);
    return propertyMapper;
}
