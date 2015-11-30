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
#include "query.h"

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

QPair<KAsync::Job<void>, typename Akonadi2::ResultEmitter<Akonadi2::ApplicationDomain::AkonadiResource::Ptr>::Ptr > ResourceFacade::load(const Akonadi2::Query &query)
{
    auto resultProvider = new Akonadi2::ResultProvider<typename Akonadi2::ApplicationDomain::AkonadiResource::Ptr>();
    auto emitter = resultProvider->emitter();
    resultProvider->setFetcher([](const QSharedPointer<Akonadi2::ApplicationDomain::AkonadiResource> &) {});
    resultProvider->onDone([resultProvider]() {
        delete resultProvider;
    });
    auto job = KAsync::start<void>([query, resultProvider]() {
        const auto configuredResources = ResourceConfig::getResources();
        for (const auto &res : configuredResources.keys()) {
            const auto type = configuredResources.value(res);
            if (!query.propertyFilter.contains("type") || query.propertyFilter.value("type").toByteArray() == type) {
                auto resource = Akonadi2::ApplicationDomain::AkonadiResource::Ptr::create();
                resource->setProperty("identifier", res);
                resource->setProperty("type", type);
                resultProvider->add(resource);
            }
        }
        //TODO initialResultSetComplete should be implicit
        resultProvider->initialResultSetComplete(Akonadi2::ApplicationDomain::AkonadiResource::Ptr());
        resultProvider->complete();
    });
    return qMakePair(job, emitter);
}

