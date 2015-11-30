/*
 * Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <Async/Async>

#include <common/domainadaptor.h>
#include <common/resultprovider.h>
#include <common/resourceaccess.h>
#include <common/facade.h>
#include <common/genericresource.h>

//Replace with something different
#include "event_generated.h"

class TestEventAdaptorFactory : public DomainTypeAdaptorFactory<Akonadi2::ApplicationDomain::Event, Akonadi2::ApplicationDomain::Buffer::Event, Akonadi2::ApplicationDomain::Buffer::EventBuilder>
{
public:
    TestEventAdaptorFactory()
        : DomainTypeAdaptorFactory()
    {
    }

    virtual ~TestEventAdaptorFactory() {};
};

class TestResourceAccess : public Akonadi2::ResourceAccessInterface
{
    Q_OBJECT
public:
    virtual ~TestResourceAccess() {};
    KAsync::Job<void> sendCommand(int commandId) Q_DECL_OVERRIDE { return KAsync::null<void>(); }
    KAsync::Job<void> sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb) Q_DECL_OVERRIDE { return KAsync::null<void>(); }
    KAsync::Job<void> synchronizeResource(bool remoteSync, bool localSync) Q_DECL_OVERRIDE { return KAsync::null<void>(); }

public Q_SLOTS:
    void open() Q_DECL_OVERRIDE {}
    void close() Q_DECL_OVERRIDE {}
};

class TestResourceFacade : public Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>
{
public:
    TestResourceFacade(const QByteArray &instanceIdentifier, const QSharedPointer<Akonadi2::ResourceAccessInterface> resourceAccess)
        : Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>(instanceIdentifier, QSharedPointer<TestEventAdaptorFactory>::create(), resourceAccess)
    {

    }
    virtual ~TestResourceFacade()
    {

    }
};

class TestResource : public Akonadi2::GenericResource
{
public:
    TestResource(const QByteArray &instanceIdentifier, QSharedPointer<Akonadi2::Pipeline> pipeline)
        : Akonadi2::GenericResource(instanceIdentifier, pipeline)
    {
    }

    KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE
    {
        return KAsync::null<void>();
    }
};
