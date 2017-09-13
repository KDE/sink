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

#include <KAsync/Async>
#include "common/resultprovider.h"
#include "common/domain/applicationdomaintype.h"
#include "common/configstore.h"
#include "common/log.h"

namespace Sink {
class Query;
class Inspection;
}

class ConfigNotifier : public QObject
{
    Q_OBJECT
public:
    void add(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &account, const QByteArray &type)
    {
        emit added(account, type);
    }

    void remove(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &account, const QByteArray &type)
    {
        emit removed(account, type);
    }

    void modify(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &account, const QByteArray &type)
    {
        emit modified(account, type);
    }
signals:
    void added(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &account, const QByteArray &type);
    void removed(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &account, const QByteArray &type);
    void modified(const Sink::ApplicationDomain::ApplicationDomainType::Ptr &account, const QByteArray &type);
};

template <typename DomainType>
class LocalStorageQueryRunner
{
public:
    LocalStorageQueryRunner(const Sink::Query &query, const QByteArray &identifier, const QByteArray &typeName, ConfigNotifier &configNotifier, const Sink::Log::Context &);
    typename Sink::ResultEmitter<typename DomainType::Ptr>::Ptr emitter();
    void setStatusUpdater(const std::function<void(DomainType &)> &);
    void statusChanged(const QByteArray &identifier);
    QObject *guard() const;
    QMap<QByteArray, QSharedPointer<Sink::ResultEmitter<QSharedPointer<Sink::ApplicationDomain::SinkResource> > > > mResourceEmitter;

private:
    void updateStatus(DomainType &entity);
    std::function<void(DomainType &)> mStatusUpdater;
    QSharedPointer<Sink::ResultProvider<typename DomainType::Ptr>> mResultProvider;
    ConfigStore mConfigStore;
    std::unique_ptr<QObject> mGuard;
    Sink::Log::Context mLogCtx;
};

template <typename DomainType>
class LocalStorageFacade : public Sink::StoreFacade<DomainType>
{
public:
    LocalStorageFacade(const QByteArray &instanceIdentifier, const QByteArray &typeName);
    virtual ~LocalStorageFacade();
    virtual KAsync::Job<void> create(const DomainType &resource) Q_DECL_OVERRIDE;
    virtual KAsync::Job<void> modify(const DomainType &resource) Q_DECL_OVERRIDE;
    virtual KAsync::Job<void> move(const DomainType &resource, const QByteArray &) Q_DECL_OVERRIDE;
    virtual KAsync::Job<void> copy(const DomainType &resource, const QByteArray &) Q_DECL_OVERRIDE;
    virtual KAsync::Job<void> remove(const DomainType &resource) Q_DECL_OVERRIDE;
    virtual QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename DomainType::Ptr>::Ptr> load(const Sink::Query &query, const Sink::Log::Context &) Q_DECL_OVERRIDE;

protected:
    QByteArray mIdentifier;
    QByteArray mTypeName;
    static ConfigNotifier sConfigNotifier;
};

class ResourceFacade : public LocalStorageFacade<Sink::ApplicationDomain::SinkResource>
{
public:
    ResourceFacade();
    virtual ~ResourceFacade();
    virtual KAsync::Job<void> remove(const Sink::ApplicationDomain::SinkResource &resource) Q_DECL_OVERRIDE;
    virtual QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename Sink::ApplicationDomain::SinkResource::Ptr>::Ptr> load(const Sink::Query &query, const Sink::Log::Context &) Q_DECL_OVERRIDE;
};

class AccountFacade : public LocalStorageFacade<Sink::ApplicationDomain::SinkAccount>
{
public:
    AccountFacade();
    virtual ~AccountFacade();
    virtual KAsync::Job<void> remove(const Sink::ApplicationDomain::SinkAccount &resource) Q_DECL_OVERRIDE;
    virtual QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename Sink::ApplicationDomain::SinkAccount::Ptr>::Ptr> load(const Sink::Query &query, const Sink::Log::Context &) Q_DECL_OVERRIDE;
};

class IdentityFacade : public LocalStorageFacade<Sink::ApplicationDomain::Identity>
{
public:
    IdentityFacade();
    virtual ~IdentityFacade();
};

