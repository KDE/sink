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

namespace Akonadi2 {
    class Query;
}

class ResourceFacade : public Akonadi2::StoreFacade<Akonadi2::ApplicationDomain::AkonadiResource>
{
public:
    ResourceFacade(const QByteArray &instanceIdentifier);
    virtual ~ResourceFacade();
    KAsync::Job<void> create(const Akonadi2::ApplicationDomain::AkonadiResource &resource) Q_DECL_OVERRIDE;
    KAsync::Job<void> modify(const Akonadi2::ApplicationDomain::AkonadiResource &resource) Q_DECL_OVERRIDE;
    KAsync::Job<void> remove(const Akonadi2::ApplicationDomain::AkonadiResource &resource) Q_DECL_OVERRIDE;
    KAsync::Job<void> load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProviderInterface<typename Akonadi2::ApplicationDomain::AkonadiResource::Ptr> > &resultProvider) Q_DECL_OVERRIDE;
};
