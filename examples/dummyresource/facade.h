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

#pragma once

#include "common/facade.h"
#include "common/domain/event.h"

class DummyResourceFacade : public Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>
{
public:
    DummyResourceFacade(const QByteArray &instanceIdentifier);
    virtual ~DummyResourceFacade();
};

class DummyResourceMailFacade : public Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Mail>
{
public:
    DummyResourceMailFacade(const QByteArray &instanceIdentifier);
    virtual ~DummyResourceMailFacade();
};

class DummyResourceFolderFacade : public Akonadi2::StoreFacade<Akonadi2::ApplicationDomain::Folder>
{
public:
    DummyResourceFolderFacade(const QByteArray &instanceIdentifier);
    virtual ~DummyResourceFolderFacade();
    virtual KAsync::Job<void> create(const Akonadi2::ApplicationDomain::Folder &domainObject) { return KAsync::null<void>(); };
    virtual KAsync::Job<void> modify(const Akonadi2::ApplicationDomain::Folder &domainObject) { return KAsync::null<void>(); };
    virtual KAsync::Job<void> remove(const Akonadi2::ApplicationDomain::Folder &domainObject) { return KAsync::null<void>(); };
    virtual KAsync::Job<void> load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Folder::Ptr> > &resultProvider);
};
