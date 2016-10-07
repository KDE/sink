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

//TODO: a little ugly to have this in two places, once here and once in Q_PLUGIN_METADATA
#define PLUGIN_NAME "sink.dummy"

class DummyResource : public Sink::GenericResource
{
public:
    DummyResource(const QByteArray &instanceIdentifier, const QSharedPointer<Sink::Pipeline> &pipeline = QSharedPointer<Sink::Pipeline>());
    virtual ~DummyResource();

    KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE;
    KAsync::Job<void> inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue) Q_DECL_OVERRIDE;
};

class DummyResourceFactory : public Sink::ResourceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "sink.dummy")
    Q_INTERFACES(Sink::ResourceFactory)

public:
    DummyResourceFactory(QObject *parent = 0);

    Sink::Resource *createResource(const QByteArray &instanceIdentifier) Q_DECL_OVERRIDE;
    void registerFacades(Sink::FacadeFactory &factory) Q_DECL_OVERRIDE;
    void registerAdaptorFactories(Sink::AdaptorFactoryRegistry &registry) Q_DECL_OVERRIDE;
    void removeDataFromDisk(const QByteArray &instanceIdentifier) Q_DECL_OVERRIDE;
};

