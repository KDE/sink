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

#pragma once

#include "sink_export.h"
#include <QByteArray>
#include <QDebug>
#include <QMutex>
#include <functional>
#include <memory>

#include "facadeinterface.h"
#include "applicationdomaintype.h"
#include "resourcecontext.h"
#include "log.h"

namespace Sink {

/**
 * Facade factory that returns a store facade implementation, by loading a plugin and providing the relevant implementation.
 *
 * If we were to provide default implementations for certain capabilities. Here would be the place to do so.
 */
class SINK_EXPORT FacadeFactory
{
public:
    typedef std::function<std::shared_ptr<void>(const ResourceContext &)> FactoryFunction;

    void registerStaticFacades();

    static FacadeFactory &instance();

    static QByteArray key(const QByteArray &resource, const QByteArray &type);

    template <class DomainType, class Facade>
    void registerFacade(const QByteArray &resource)
    {
        registerFacade(resource, [](const ResourceContext &context) { return std::make_shared<Facade>(context); }, ApplicationDomain::getTypeName<DomainType>());
    }

    template <class DomainType, class Facade>
    void registerFacade()
    {
        registerFacade(QByteArray(), [](const ResourceContext &) { return std::make_shared<Facade>(); }, ApplicationDomain::getTypeName<DomainType>());
    }

    /*
     * Allows the registrar to register a specific instance.
     *
     * Primarily for testing.
     */
    template <class DomainType, class Facade>
    void registerFacade(const QByteArray &resource, const FactoryFunction &customFactoryFunction)
    {
        registerFacade(resource, customFactoryFunction, ApplicationDomain::getTypeName<DomainType>());
    }

    /*
     * Can be used to clear the factory.
     *
     * Primarily for testing.
     */
    void resetFactory();

    template <class DomainType>
    std::shared_ptr<StoreFacade<DomainType>> getFacade(const QByteArray &resource, const QByteArray &instanceIdentifier)
    {
        const QByteArray typeName = ApplicationDomain::getTypeName<DomainType>();
        const auto ptr = getFacade(resource, instanceIdentifier, typeName);
        //We have to check the pointer before the cast, otherwise a check would return true also for invalid instances.
        if (!ptr) {
            return std::shared_ptr<StoreFacade<DomainType>>();
        }
        return std::static_pointer_cast<StoreFacade<DomainType>>(ptr);
    }

    template <class DomainType>
    std::shared_ptr<StoreFacade<DomainType>> getFacade()
    {
        return getFacade<DomainType>(QByteArray(), QByteArray());
    }

private:
    FacadeFactory();
    std::shared_ptr<void> getFacade(const QByteArray &resource, const QByteArray &instanceIdentifier, const QByteArray &typeName);
    void registerFacade(const QByteArray &resource, const FactoryFunction &customFactoryFunction, const QByteArray typeName);

    QHash<QByteArray, FactoryFunction> mFacadeRegistry;
    static QMutex sMutex;
};
}
