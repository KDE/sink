/*
 * Copyright (C) 2016 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include "adaptorfactoryregistry.h"

#include <QByteArray>
#include <QDebug>
#include <QMutex>
#include <functional>
#include <memory>

#include "domaintypeadaptorfactoryinterface.h"
#include "applicationdomaintype.h"
#include "log.h"

using namespace Sink;

AdaptorFactoryRegistry &AdaptorFactoryRegistry::instance()
{
    // QMutexLocker locker(&sMutex);
    static AdaptorFactoryRegistry *instance = 0;
    if (!instance) {
        instance = new AdaptorFactoryRegistry;
    }
    return *instance;
}

static QByteArray key(const QByteArray &resource, const QByteArray &type)
{
    return resource + type;
}

AdaptorFactoryRegistry::AdaptorFactoryRegistry()
{

}

std::shared_ptr<DomainTypeAdaptorFactoryInterface> AdaptorFactoryRegistry::getFactory(const QByteArray &resource, const QByteArray &typeName)
{
    const auto ptr = mRegistry.value(key(resource, typeName));
    //We have to check the pointer before the cast, otherwise a check would return true also for invalid instances.
    if (!ptr) {
        return std::shared_ptr<DomainTypeAdaptorFactoryInterface>();
    }
    return std::static_pointer_cast<DomainTypeAdaptorFactoryInterface>(ptr);
}

QMap<QByteArray, DomainTypeAdaptorFactoryInterface::Ptr> AdaptorFactoryRegistry::getFactories(const QByteArray &resource)
{
    QMap<QByteArray, DomainTypeAdaptorFactoryInterface::Ptr> map;
    for (const auto &type : mTypes.values(resource)) {
        auto f = getFactory(resource, type);
        //Convert the std::shared_ptr to a QSharedPointer
        map.insert(type, DomainTypeAdaptorFactoryInterface::Ptr(f.get(), [](DomainTypeAdaptorFactoryInterface *) {}));
    }
    return map;
}

void AdaptorFactoryRegistry::registerFactory(const QByteArray &resource, const std::shared_ptr<void> &instance, const QByteArray typeName)
{
    mTypes.insert(resource, typeName);
    mRegistry.insert(key(resource, typeName), instance);
}

