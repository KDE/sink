/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
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

#pragma once

#include "common/resource.h"
#include "async/src/async.h"

#include <flatbuffers/flatbuffers.h>

//TODO: a little ugly to have this in two places, once here and once in Q_PLUGIN_METADATA
#define PLUGIN_NAME "org.kde.dummy"

class DummyResource : public Akonadi2::Resource
{
public:
    DummyResource();
    Async::Job<void> synchronizeWithSource(Akonadi2::Pipeline *pipeline);
    void processCommand(int commandId, const QByteArray &data, uint size, Akonadi2::Pipeline *pipeline);

private:
    flatbuffers::FlatBufferBuilder m_fbb;
};

class DummyResourceFactory : public Akonadi2::ResourceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.dummy")
    Q_INTERFACES(Akonadi2::ResourceFactory)

public:
    DummyResourceFactory(QObject *parent = 0);

    Akonadi2::Resource *createResource();
    void registerFacades(Akonadi2::FacadeFactory &factory);
};

