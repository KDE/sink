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

#include "common/genericresource.h"
#include "common/messagequeue.h"

#include <Async/Async>

#include <flatbuffers/flatbuffers.h>

class DummyResource : public Sink::GenericResource
{
public:
    DummyResource(const Sink::ResourceContext &resourceContext, const QSharedPointer<Sink::Pipeline> &pipeline = QSharedPointer<Sink::Pipeline>());
    virtual ~DummyResource();
};

class DummyResourceFactory : public Sink::ResourceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "sink.dummy")
    Q_INTERFACES(Sink::ResourceFactory)

public:
    DummyResourceFactory(QObject *parent = 0);

    Sink::Resource *createResource(const Sink::ResourceContext &resourceContext) Q_DECL_OVERRIDE;
    void registerFacades(const QByteArray &resourceName, Sink::FacadeFactory &factory) Q_DECL_OVERRIDE;
    void registerAdaptorFactories(const QByteArray &resourceName, Sink::AdaptorFactoryRegistry &registry) Q_DECL_OVERRIDE;
    void removeDataFromDisk(const QByteArray &instanceIdentifier) Q_DECL_OVERRIDE;
};

