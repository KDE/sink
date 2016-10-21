/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include "facadefactory.h"

#include "resourcefacade.h"
#include "resource.h"
#include "adaptorfactoryregistry.h"

using namespace Sink;

QMutex FacadeFactory::sMutex;

FacadeFactory::FacadeFactory()
{
    registerStaticFacades();
}

FacadeFactory &FacadeFactory::instance()
{
    QMutexLocker locker(&sMutex);
    static FacadeFactory *instance = 0;
    if (!instance) {
        instance = new FacadeFactory;
    }
    return *instance;
}

QByteArray FacadeFactory::key(const QByteArray &resource, const QByteArray &type)
{
    return resource + type;
}

void FacadeFactory::resetFactory()
{
    QMutexLocker locker(&sMutex);
    mFacadeRegistry.clear();
}

void FacadeFactory::registerStaticFacades()
{
    registerFacade<Sink::ApplicationDomain::SinkResource, ResourceFacade>();
    registerFacade<Sink::ApplicationDomain::SinkAccount, AccountFacade>();
    registerFacade<Sink::ApplicationDomain::Identity, IdentityFacade>();
}

std::shared_ptr<void> FacadeFactory::getFacade(const QByteArray &resource, const QByteArray &instanceIdentifier, const QByteArray &typeName)
{
    QMutexLocker locker(&sMutex);

    const QByteArray k = key(resource, typeName);
    if (!mFacadeRegistry.contains(k)) {
        locker.unlock();
        // This will call FacadeFactory::instace() internally
        Sink::ResourceFactory::load(resource);
        locker.relock();
    }

    if (auto factoryFunction = mFacadeRegistry.value(k)) {
        return factoryFunction(ResourceContext{instanceIdentifier, resource, AdaptorFactoryRegistry::instance().getFactories(resource)});
    }
    qWarning() << "Failed to find facade for resource: " << resource << " and type: " << typeName;
    return std::shared_ptr<void>();
}

void FacadeFactory::registerFacade(const QByteArray &resource, const FactoryFunction &customFactoryFunction, const QByteArray typeName)
{
    mFacadeRegistry.insert(key(resource, typeName), customFactoryFunction);
}
