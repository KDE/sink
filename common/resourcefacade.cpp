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
#include <QStandardPaths>

ResourceFacade::ResourceFacade(const QByteArray &)
    : Akonadi2::StoreFacade<Akonadi2::ApplicationDomain::AkonadiResource>()
{

}

ResourceFacade::~ResourceFacade()
{

}

static QSharedPointer<QSettings> getSettings()
{
    return QSharedPointer<QSettings>::create(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/resources.ini", QSettings::IniFormat);
}

KAsync::Job<void> ResourceFacade::create(const Akonadi2::ApplicationDomain::AkonadiResource &resource)
{
    return KAsync::start<void>([resource, this]() {
        auto settings = getSettings();
        const QByteArray identifier = resource.getProperty("identifier").toByteArray();
        const QByteArray type = resource.getProperty("type").toByteArray();

        settings->beginGroup("resources");
        settings->setValue(identifier, type);
        settings->endGroup();
        settings->beginGroup(identifier);
        //Add some settings?
        settings->endGroup();
        settings->sync();
    });
}

KAsync::Job<void> ResourceFacade::modify(const Akonadi2::ApplicationDomain::AkonadiResource &resource)
{
    return KAsync::null<void>();
}

KAsync::Job<void> ResourceFacade::remove(const Akonadi2::ApplicationDomain::AkonadiResource &resource)
{
    return KAsync::start<void>([resource, this]() {
        auto settings = getSettings();
        const QByteArray identifier = resource.getProperty("identifier").toByteArray();

        settings->beginGroup("resources");
        settings->remove(identifier);
        settings->endGroup();
        settings->sync();
    });
}

KAsync::Job<void> ResourceFacade::load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<typename Akonadi2::ApplicationDomain::AkonadiResource::Ptr> > &resultProvider)
{
    return KAsync::start<void>([query, resultProvider]() {
        auto settings = getSettings();
        settings->beginGroup("resources");
        for (const auto &identifier : settings->childKeys()) {
            const auto type = settings->value(identifier).toByteArray();
            auto resource = Akonadi2::ApplicationDomain::AkonadiResource::Ptr::create();
            resource->setProperty("identifier", identifier);
            resource->setProperty("type", type);
            resultProvider->add(resource);
        }
        settings->endGroup();

        //TODO initialResultSetComplete should be implicit
        resultProvider->initialResultSetComplete();
        resultProvider->complete();
    });
}

