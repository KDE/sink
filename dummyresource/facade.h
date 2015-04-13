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

#include "common/facade.h"

#include "common/clientapi.h"
#include "common/storage.h"
#include "resourcefactory.h"
#include "entity_generated.h"
#include "event_generated.h"
#include "dummycalendar_generated.h"
#include "common/domainadaptor.h"

class DummyResourceFacade : public Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>
{
public:
    DummyResourceFacade();
    virtual ~DummyResourceFacade();
    Async::Job<void> create(const Akonadi2::ApplicationDomain::Event &domainObject) Q_DECL_OVERRIDE;
    Async::Job<void> modify(const Akonadi2::ApplicationDomain::Event &domainObject) Q_DECL_OVERRIDE;
    Async::Job<void> remove(const Akonadi2::ApplicationDomain::Event &domainObject) Q_DECL_OVERRIDE;
    Async::Job<qint64> load(const Akonadi2::Query &query, const std::function<void(const Akonadi2::ApplicationDomain::Event::Ptr &)> &resultCallback) Q_DECL_OVERRIDE;
    Async::Job<void> load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> > &resultProvider) Q_DECL_OVERRIDE;

private:
    void readValue(QSharedPointer<Akonadi2::Storage> storage, const QByteArray &key, const std::function<void(const Akonadi2::ApplicationDomain::Event::Ptr &)> &resultCallback, std::function<bool(const std::string &key, DummyCalendar::DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local)>);
    QSharedPointer<DomainTypeAdaptorFactory<Akonadi2::ApplicationDomain::Event, Akonadi2::ApplicationDomain::Buffer::Event, DummyCalendar::DummyEvent, Akonadi2::ApplicationDomain::Buffer::EventBuilder, DummyCalendar::DummyEventBuilder> > mFactory;
};
