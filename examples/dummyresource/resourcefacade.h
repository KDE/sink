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

#include "common/clientapi.h"

#include <Async/Async>

class QSettings;

class DummyResourceConfigFacade : public Akonadi2::StoreFacade<Akonadi2::ApplicationDomain::AkonadiResource>
{
public:
    DummyResourceConfigFacade();
    ~DummyResourceConfigFacade();
    //Create an instance
    KAsync::Job<void> create(const Akonadi2::ApplicationDomain::AkonadiResource &domainObject) Q_DECL_OVERRIDE;
    //Modify configuration
    KAsync::Job<void> modify(const Akonadi2::ApplicationDomain::AkonadiResource &domainObject) Q_DECL_OVERRIDE;
    //Remove instance
    KAsync::Job<void> remove(const Akonadi2::ApplicationDomain::AkonadiResource &domainObject) Q_DECL_OVERRIDE;
    //Read configuration and available instances
    KAsync::Job<void> load(const Akonadi2::Query &query, const QSharedPointer<async::ResultProvider<typename Akonadi2::ApplicationDomain::AkonadiResource::Ptr> > &resultProvider) Q_DECL_OVERRIDE;

private:
    QSharedPointer<QSettings> getSettings();
};
