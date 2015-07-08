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
#include "resourcefacade.h"

#include "resourceconfig.h"

ResourceFacade::ResourceFacade(const QByteArray &)
    : Akonadi2::StoreFacade<Akonadi2::ApplicationDomain::AkonadiResource>()
{

}

ResourceFacade::~ResourceFacade()
{

}

KAsync::Job<void> ResourceFacade::create(const Akonadi2::ApplicationDomain::AkonadiResource &resource)
{
    return KAsync::start<void>([resource, this]() {
        const QByteArray identifier = resource.getProperty("identifier").toByteArray();
        const QByteArray type = resource.getProperty("type").toByteArray();
        ResourceConfig::addResource(identifier, type);
    });
}

KAsync::Job<void> ResourceFacade::modify(const Akonadi2::ApplicationDomain::AkonadiResource &resource)
{
    return KAsync::null<void>();
}

KAsync::Job<void> ResourceFacade::remove(const Akonadi2::ApplicationDomain::AkonadiResource &resource)
{
    return KAsync::start<void>([resource, this]() {
        const QByteArray identifier = resource.getProperty("identifier").toByteArray();
        ResourceConfig::removeResource(identifier);
    });
}

KAsync::Job<void> ResourceFacade::load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<typename Akonadi2::ApplicationDomain::AkonadiResource::Ptr> > &resultProvider)
{
    return KAsync::start<void>([query, resultProvider]() {
        for (const auto &res : ResourceConfig::getResources()) {
            auto resource = Akonadi2::ApplicationDomain::AkonadiResource::Ptr::create();
            resource->setProperty("identifier", res.first);
            resource->setProperty("type", res.second);
            resultProvider->add(resource);
        }
        //TODO initialResultSetComplete should be implicit
        resultProvider->initialResultSetComplete();
        resultProvider->complete();
    });
}

