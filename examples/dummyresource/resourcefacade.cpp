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

#include <QSettings>

DummyResourceConfigFacade::DummyResourceConfigFacade()
    : Akonadi2::StoreFacade<Akonadi2::ApplicationDomain::AkonadiResource>()
{

}

DummyResourceConfigFacade::~DummyResourceConfigFacade()
{

}

QSharedPointer<QSettings> DummyResourceConfigFacade::getSettings()
{
    //FIXME deal with resource instances
    const QString instanceIdentifier = "dummyresource.instance1";
    //FIXME Use config location
    return QSharedPointer<QSettings>::create(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/" + "org.kde." + instanceIdentifier + "/settings.ini", QSettings::IniFormat);
}

Async::Job<void> DummyResourceConfigFacade::create(const Akonadi2::ApplicationDomain::AkonadiResource &domainObject)
{
    //TODO create resource instance
    //This can be generalized in a base implementation
    return Async::null<void>();
}

Async::Job<void> DummyResourceConfigFacade::modify(const Akonadi2::ApplicationDomain::AkonadiResource &domainObject)
{
    //modify configuration
    //This part is likely resource specific, but could be partially generalized
    return Async::start<void>([domainObject, this]() {
        auto settings = getSettings();
        //TODO Write properties to file
    });
}

Async::Job<void> DummyResourceConfigFacade::remove(const Akonadi2::ApplicationDomain::AkonadiResource &domainObject)
{
    //TODO remove resource instance
    //This can be generalized in a base implementation
    return Async::null<void>();
}

Async::Job<void> DummyResourceConfigFacade::load(const Akonadi2::Query &query, const QSharedPointer<async::ResultProvider<typename Akonadi2::ApplicationDomain::AkonadiResource::Ptr> > &resultProvider)
{
    //Read configuration and list all available instances.
    //This includes runtime information about runing instances etc.
    //Part of this is generic, and part is accessing the resource specific configuration.
    //FIXME this currently does not support live queries (because we're not inheriting from GenericFacade)
    //FIXME only read what was requested in the query?
    return Async::start<void>([resultProvider, this]() {
        auto settings = getSettings();
        auto memoryAdaptor = QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create();
        //TODO copy settings to adaptor
        //
        //TODO use correct instance identifier
        //TODO key == instance identifier ?
        resultProvider->add(QSharedPointer<Akonadi2::ApplicationDomain::AkonadiResource>::create("org.kde.dummy", "org.kde.dummy.config", 0, memoryAdaptor));
    });
}
