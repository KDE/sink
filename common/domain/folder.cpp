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
#include "entitybuffer.h"
#include "datastorequery.h"
#include "entity_generated.h"

#include "folder_generated.h"

SINK_DEBUG_AREA("folder");

static QMutex sMutex;

using namespace Sink::ApplicationDomain;

void TypeImplementation<Folder>::configureIndex(TypeIndex &index)
{
    index.addProperty<QByteArray>(Folder::Parent::name);
    index.addProperty<QString>(Folder::Name::name);
}

static TypeIndex &getIndex()
{
    QMutexLocker locker(&sMutex);
    static TypeIndex *index = 0;
    if (!index) {
        index = new TypeIndex("folder");
        TypeImplementation<Folder>::configureIndex(*index);
    }
    return *index;
}

void TypeImplementation<Folder>::index(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::DataStore::Transaction &transaction)
{
    SinkTrace() << "Indexing " << identifier;
    getIndex().add(identifier, bufferAdaptor, transaction);
}

void TypeImplementation<Folder>::removeIndex(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::DataStore::Transaction &transaction)
{
    getIndex().remove(identifier, bufferAdaptor, transaction);
}

QSharedPointer<ReadPropertyMapper<TypeImplementation<Folder>::Buffer> > TypeImplementation<Folder>::initializeReadPropertyMapper()
{
    auto propertyMapper = QSharedPointer<ReadPropertyMapper<Buffer> >::create();
    propertyMapper->addMapping<Folder::Parent, Buffer>(&Buffer::parent);
    propertyMapper->addMapping<Folder::Name, Buffer>(&Buffer::name);
    propertyMapper->addMapping<Folder::Icon, Buffer>(&Buffer::icon);
    propertyMapper->addMapping<Folder::SpecialPurpose, Buffer>(&Buffer::specialpurpose);
    return propertyMapper;
}

QSharedPointer<WritePropertyMapper<TypeImplementation<Folder>::BufferBuilder> > TypeImplementation<Folder>::initializeWritePropertyMapper()
{
    auto propertyMapper = QSharedPointer<WritePropertyMapper<BufferBuilder> >::create();
    propertyMapper->addMapping<Folder::Parent>(&BufferBuilder::add_parent);
    propertyMapper->addMapping<Folder::Name>(&BufferBuilder::add_name);
    propertyMapper->addMapping<Folder::Icon>(&BufferBuilder::add_icon);
    propertyMapper->addMapping<Folder::SpecialPurpose>(&BufferBuilder::add_specialpurpose);
    return propertyMapper;
}
