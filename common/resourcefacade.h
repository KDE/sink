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

#include "common/facadeinterface.h"

#include <Async/Async>
#include "common/resultprovider.h"
#include "common/domain/applicationdomaintype.h"

namespace Sink {
class Query;
class Inspection;
}

class ResourceFacade : public Sink::StoreFacade<Sink::ApplicationDomain::SinkResource>
{
public:
    ResourceFacade(const QByteArray &instanceIdentifier);
    virtual ~ResourceFacade();
    KAsync::Job<void> create(const Sink::ApplicationDomain::SinkResource &resource) Q_DECL_OVERRIDE;
    KAsync::Job<void> modify(const Sink::ApplicationDomain::SinkResource &resource) Q_DECL_OVERRIDE;
    KAsync::Job<void> remove(const Sink::ApplicationDomain::SinkResource &resource) Q_DECL_OVERRIDE;
    QPair<KAsync::Job<void>, typename Sink::ResultEmitter<Sink::ApplicationDomain::SinkResource::Ptr>::Ptr> load(const Sink::Query &query) Q_DECL_OVERRIDE;
};
