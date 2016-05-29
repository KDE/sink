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
#include <common/commands.h>

// Replace with something different
#include "event_generated.h"
#include "mail_generated.h"
#include "createentity_generated.h"

class TestEventAdaptorFactory : public DomainTypeAdaptorFactory<Sink::ApplicationDomain::Event, Sink::ApplicationDomain::Buffer::Event, Sink::ApplicationDomain::Buffer::EventBuilder>
{
public:
    TestEventAdaptorFactory() : DomainTypeAdaptorFactory()
    {
    }

    virtual ~TestEventAdaptorFactory(){};
};

class TestMailAdaptorFactory : public DomainTypeAdaptorFactory<Sink::ApplicationDomain::Mail, Sink::ApplicationDomain::Buffer::Mail, Sink::ApplicationDomain::Buffer::MailBuilder>
{
public:
    TestMailAdaptorFactory() : DomainTypeAdaptorFactory()
    {
    }

    virtual ~TestMailAdaptorFactory(){};
};

class TestResourceAccess : public Sink::ResourceAccessInterface
{
    Q_OBJECT
public:
    virtual ~TestResourceAccess(){};
    KAsync::Job<void> sendCommand(int commandId) Q_DECL_OVERRIDE
    {
        return KAsync::null<void>();
    }
    KAsync::Job<void> sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb) Q_DECL_OVERRIDE
    {
        return KAsync::null<void>();
    }
    KAsync::Job<void> synchronizeResource(bool remoteSync, bool localSync) Q_DECL_OVERRIDE
    {
        return KAsync::null<void>();
    }

public slots:
    void open() Q_DECL_OVERRIDE
    {
    }
    void close() Q_DECL_OVERRIDE
    {
    }
};

class TestResourceFacade : public Sink::GenericFacade<Sink::ApplicationDomain::Event>
{
public:
    TestResourceFacade(const QByteArray &instanceIdentifier, const QSharedPointer<Sink::ResourceAccessInterface> resourceAccess)
        : Sink::GenericFacade<Sink::ApplicationDomain::Event>(instanceIdentifier, QSharedPointer<TestEventAdaptorFactory>::create(), resourceAccess)
    {
    }
    virtual ~TestResourceFacade()
    {
    }
};

class TestMailResourceFacade : public Sink::GenericFacade<Sink::ApplicationDomain::Mail>
{
public:
    TestMailResourceFacade(const QByteArray &instanceIdentifier, const QSharedPointer<Sink::ResourceAccessInterface> resourceAccess)
        : Sink::GenericFacade<Sink::ApplicationDomain::Mail>(instanceIdentifier, QSharedPointer<TestMailAdaptorFactory>::create(), resourceAccess)
    {
    }
    virtual ~TestMailResourceFacade()
    {
    }
};

class TestResource : public Sink::GenericResource
{
public:
    TestResource(const QByteArray &instanceIdentifier, QSharedPointer<Sink::Pipeline> pipeline) : Sink::GenericResource("test", instanceIdentifier, pipeline)
    {
    }

    KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE
    {
        return KAsync::null<void>();
    }
};

template <typename DomainType>
QByteArray createCommand(const DomainType &domainObject, DomainTypeAdaptorFactoryInterface &domainTypeAdaptorFactory)
{
    flatbuffers::FlatBufferBuilder entityFbb;
    domainTypeAdaptorFactory.createBuffer(domainObject, entityFbb);
    flatbuffers::FlatBufferBuilder fbb;
    auto type = fbb.CreateString(Sink::ApplicationDomain::getTypeName<DomainType>().toStdString().data());
    auto delta = fbb.CreateVector<uint8_t>(entityFbb.GetBufferPointer(), entityFbb.GetSize());
    Sink::Commands::CreateEntityBuilder builder(fbb);
    builder.add_domainType(type);
    builder.add_delta(delta);
    auto location = builder.Finish();
    Sink::Commands::FinishCreateEntityBuffer(fbb, location);
    return QByteArray(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
}
