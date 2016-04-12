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

#include "common/facadeinterface.h"

#include <Async/Async>
#include "common/resultprovider.h"
#include "common/domain/applicationdomaintype.h"
#include "common/configstore.h"

namespace Sink {
class Query;
class Inspection;
}

class ConfigNotifier : public QObject
{
    Q_OBJECT
public:
    void add(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &account)
    {
        emit added(account);
    }

    void remove(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &account)
    {
        emit removed(account);
    }

    void modify(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &account)
    {
        emit modified(account);
    }
signals:
    void added(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &account);
    void removed(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &account);
    void modified(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &account);
};

template <typename DomainType>
class LocalStorageFacade : public Sink::StoreFacade<DomainType>
{
public:
    LocalStorageFacade(const QByteArray &instanceIdentifier);
    virtual ~LocalStorageFacade();
    virtual KAsync::Job<void> create(const DomainType &resource) Q_DECL_OVERRIDE;
    virtual KAsync::Job<void> modify(const DomainType &resource) Q_DECL_OVERRIDE;
    virtual KAsync::Job<void> remove(const DomainType &resource) Q_DECL_OVERRIDE;
    virtual QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename DomainType::Ptr>::Ptr> load(const Sink::Query &query) Q_DECL_OVERRIDE;
private:
    typename DomainType::Ptr readFromConfig(const QByteArray &id, const QByteArray &type);

    ConfigStore mConfigStore;
    static ConfigNotifier sConfigNotifier;
};

class ResourceFacade : public LocalStorageFacade<Sink::ApplicationDomain::SinkResource>
{
public:
    ResourceFacade(const QByteArray &instanceIdentifier);
    virtual ~ResourceFacade();
    virtual KAsync::Job<void> remove(const Sink::ApplicationDomain::SinkResource &resource) Q_DECL_OVERRIDE;
};

class AccountFacade : public LocalStorageFacade<Sink::ApplicationDomain::SinkAccount>
{
public:
    AccountFacade(const QByteArray &instanceIdentifier);
    virtual ~AccountFacade();
};


