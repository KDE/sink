/*
 * Copyright (C) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
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

#include "domaintypeadaptorfactoryinterface.h"
#include "applicationdomaintype.h"
#include "resourceaccess.h"
#include <QByteArray>
#include <QMap>

namespace Sink {

/*
 * A context object that can be passed around so each part of the system knows in what context it works.
 *
 * This is necessary because we can't rely on a singleton or thread-local storage since multiple resources can be accessed from the same thread/process.
 */
struct ResourceContext {
    const QByteArray resourceInstanceIdentifier;
    const QByteArray resourceType;
    QMap<QByteArray, DomainTypeAdaptorFactoryInterface::Ptr> adaptorFactories;
    //TODO prehaps use a weak pointer to not mess up lifetime management
    ResourceAccessInterface::Ptr mResourceAccess;


    ResourceContext(const QByteArray &identifier, const QByteArray &resourceType_, const QMap<QByteArray, DomainTypeAdaptorFactoryInterface::Ptr> &factories = QMap<QByteArray, DomainTypeAdaptorFactoryInterface::Ptr>())
        : resourceInstanceIdentifier(identifier),
        resourceType(resourceType_),
        adaptorFactories(factories)
    {
    }

    QByteArray instanceId() const
    {
        return resourceInstanceIdentifier;
    }

    DomainTypeAdaptorFactoryInterface &adaptorFactory(const QByteArray &type) const
    {
        auto factory = adaptorFactories.value(type);
        Q_ASSERT(factory);
        return *factory;
    }

    template<typename DomainType>
    DomainTypeAdaptorFactoryInterface &adaptorFactory()
    {
        return adaptorFactory(ApplicationDomain::getTypeName<DomainType>());
    }

    ResourceAccessInterface::Ptr resourceAccess()
    {
        if (!mResourceAccess) {
            mResourceAccess = ResourceAccessFactory::instance().getAccess(resourceInstanceIdentifier, resourceType);
        }
        return mResourceAccess;
    }
};

}
