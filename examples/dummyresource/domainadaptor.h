/*
 * Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "common/domainadaptor.h"
#include "event_generated.h"
#include "mail_generated.h"
#include "folder_generated.h"
#include "entity_generated.h"

class DummyEventAdaptorFactory : public DomainTypeAdaptorFactory<Sink::ApplicationDomain::Event>
{
public:
    DummyEventAdaptorFactory();
    virtual ~DummyEventAdaptorFactory() {};
};

class DummyMailAdaptorFactory : public DomainTypeAdaptorFactory<Sink::ApplicationDomain::Mail>
{
public:
    DummyMailAdaptorFactory();
    virtual ~DummyMailAdaptorFactory() {};
};

class DummyFolderAdaptorFactory : public DomainTypeAdaptorFactory<Sink::ApplicationDomain::Folder>
{
public:
    DummyFolderAdaptorFactory();
    virtual ~DummyFolderAdaptorFactory() {};
};
