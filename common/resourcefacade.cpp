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

#include "resourceconfig.h"
#include "query.h"
#include "definitions.h"
#include "storage.h"
#include <QDir>

ResourceFacade::ResourceFacade(const QByteArray &) : Sink::StoreFacade<Sink::ApplicationDomain::SinkResource>()
{
}

ResourceFacade::~ResourceFacade()
{
}

KAsync::Job<void> ResourceFacade::create(const Sink::ApplicationDomain::SinkResource &resource)
{
    return KAsync::start<void>([resource, this]() {
        const QByteArray type = resource.getProperty("type").toByteArray();
        const QByteArray providedIdentifier = resource.getProperty("identifier").toByteArray();
        // It is currently a requirement that the resource starts with the type
        const QByteArray identifier = providedIdentifier.isEmpty() ? ResourceConfig::newIdentifier(type) : providedIdentifier;
        ResourceConfig::addResource(identifier, type);
        auto changedProperties = resource.changedProperties();
        changedProperties.removeOne("identifier");
        changedProperties.removeOne("type");
        if (!changedProperties.isEmpty()) {
            // We have some configuration values
            QMap<QByteArray, QVariant> configurationValues;
            for (const auto &property : changedProperties) {
                configurationValues.insert(property, resource.getProperty(property));
            }
            ResourceConfig::configureResource(identifier, configurationValues);
        }
    });
}

KAsync::Job<void> ResourceFacade::modify(const Sink::ApplicationDomain::SinkResource &resource)
{
    return KAsync::start<void>([resource, this]() {
        const QByteArray identifier = resource.identifier();
        if (identifier.isEmpty()) {
            Warning() << "We need an \"identifier\" property to identify the resource to configure.";
            return;
        }
        auto changedProperties = resource.changedProperties();
        changedProperties.removeOne("identifier");
        changedProperties.removeOne("type");
        if (!changedProperties.isEmpty()) {
            // We have some configuration values
            QMap<QByteArray, QVariant> configurationValues;
            for (const auto &property : changedProperties) {
                configurationValues.insert(property, resource.getProperty(property));
            }
            ResourceConfig::configureResource(identifier, configurationValues);
        }
    });
}

KAsync::Job<void> ResourceFacade::remove(const Sink::ApplicationDomain::SinkResource &resource)
{
    return KAsync::start<void>([resource, this]() {
        const QByteArray identifier = resource.identifier();
        if (identifier.isEmpty()) {
            Warning() << "We need an \"identifier\" property to identify the resource to configure";
            return;
        }
        ResourceConfig::removeResource(identifier);
        // TODO shutdown resource, or use the resource process with a --remove option to cleanup (so we can take advantage of the file locking)
        QDir dir(Sink::storageLocation());
        for (const auto &folder : dir.entryList(QStringList() << identifier + "*")) {
            Sink::Storage(Sink::storageLocation(), folder, Sink::Storage::ReadWrite).removeFromDisk();
        }
    });
}

QPair<KAsync::Job<void>, typename Sink::ResultEmitter<Sink::ApplicationDomain::SinkResource::Ptr>::Ptr> ResourceFacade::load(const Sink::Query &query)
{
    auto resultProvider = new Sink::ResultProvider<typename Sink::ApplicationDomain::SinkResource::Ptr>();
    auto emitter = resultProvider->emitter();
    resultProvider->setFetcher([](const QSharedPointer<Sink::ApplicationDomain::SinkResource> &) {});
    resultProvider->onDone([resultProvider]() { delete resultProvider; });
    auto job = KAsync::start<void>([query, resultProvider]() {
        const auto configuredResources = ResourceConfig::getResources();
        for (const auto &res : configuredResources.keys()) {
            const auto type = configuredResources.value(res);
            if (query.propertyFilter.contains("type") && query.propertyFilter.value("type").toByteArray() != type) {
                continue;
            }
            if (!query.ids.isEmpty() && !query.ids.contains(res)) {
                continue;
            }

            auto resource = Sink::ApplicationDomain::SinkResource::Ptr::create(res);
            resource->setProperty("type", type);

            const auto configurationValues = ResourceConfig::getConfiguration(res);
            for (auto it = configurationValues.constBegin(); it != configurationValues.constEnd(); it++) {
                resource->setProperty(it.key(), it.value());
            }

            resultProvider->add(resource);
        }
        // TODO initialResultSetComplete should be implicit
        resultProvider->initialResultSetComplete(Sink::ApplicationDomain::SinkResource::Ptr());
        resultProvider->complete();
    });
    return qMakePair(job, emitter);
}



Q_GLOBAL_STATIC(ConfigNotifier, sConfig);

static Sink::ApplicationDomain::SinkAccount::Ptr readAccountFromConfig(const QByteArray &id, const QByteArray &type)
{
    auto account = Sink::ApplicationDomain::SinkAccount::Ptr::create(id);

    account->setProperty("type", type);
    const auto configurationValues = AccountConfig::getConfiguration(id);
    for (auto it = configurationValues.constBegin(); it != configurationValues.constEnd(); it++) {
        account->setProperty(it.key(), it.value());
    }
    return account;
}


AccountFacade::AccountFacade(const QByteArray &) : Sink::StoreFacade<Sink::ApplicationDomain::SinkAccount>()
{
}

AccountFacade::~AccountFacade()
{
}

KAsync::Job<void> AccountFacade::create(const Sink::ApplicationDomain::SinkAccount &account)
{
    return KAsync::start<void>([account, this]() {
        const QByteArray type = account.getProperty("type").toByteArray();
        const QByteArray providedIdentifier = account.getProperty("identifier").toByteArray();
        // It is currently a requirement that the account starts with the type
        const QByteArray identifier = providedIdentifier.isEmpty() ? AccountConfig::newIdentifier(type) : providedIdentifier;
        AccountConfig::addAccount(identifier, type);
        auto changedProperties = account.changedProperties();
        changedProperties.removeOne("identifier");
        changedProperties.removeOne("type");
        if (!changedProperties.isEmpty()) {
            // We have some configuration values
            QMap<QByteArray, QVariant> configurationValues;
            for (const auto &property : changedProperties) {
                configurationValues.insert(property, account.getProperty(property));
            }
            AccountConfig::configureAccount(identifier, configurationValues);
        }
        sConfig->add(readAccountFromConfig(identifier, type));
    });
}

KAsync::Job<void> AccountFacade::modify(const Sink::ApplicationDomain::SinkAccount &account)
{
    return KAsync::start<void>([account, this]() {
        const QByteArray identifier = account.identifier();
        if (identifier.isEmpty()) {
            Warning() << "We need an \"identifier\" property to identify the account to configure.";
            return;
        }
        auto changedProperties = account.changedProperties();
        changedProperties.removeOne("identifier");
        changedProperties.removeOne("type");
        if (!changedProperties.isEmpty()) {
            // We have some configuration values
            QMap<QByteArray, QVariant> configurationValues;
            for (const auto &property : changedProperties) {
                configurationValues.insert(property, account.getProperty(property));
            }
            AccountConfig::configureAccount(identifier, configurationValues);
        }

        const auto type = AccountConfig::getAccounts().value(identifier);
        sConfig->modify(readAccountFromConfig(identifier, type));
    });
}

KAsync::Job<void> AccountFacade::remove(const Sink::ApplicationDomain::SinkAccount &account)
{
    return KAsync::start<void>([account, this]() {
        const QByteArray identifier = account.identifier();
        if (identifier.isEmpty()) {
            Warning() << "We need an \"identifier\" property to identify the account to configure";
            return;
        }
        AccountConfig::removeAccount(identifier);
        sConfig->remove(Sink::ApplicationDomain::SinkAccount::Ptr::create(account));
    });
}

QPair<KAsync::Job<void>, typename Sink::ResultEmitter<Sink::ApplicationDomain::SinkAccount::Ptr>::Ptr> AccountFacade::load(const Sink::Query &query)
{
    QObject *guard = new QObject;
    auto resultProvider = new Sink::ResultProvider<typename Sink::ApplicationDomain::SinkAccount::Ptr>();
    auto emitter = resultProvider->emitter();
    resultProvider->setFetcher([](const QSharedPointer<Sink::ApplicationDomain::SinkAccount> &) {});
    resultProvider->onDone([=]() { delete resultProvider; delete guard; });
    auto job = KAsync::start<void>([=]() {
        const auto configuredAccounts = AccountConfig::getAccounts();
        for (const auto &res : configuredAccounts.keys()) {
            const auto type = configuredAccounts.value(res);
            if (!query.ids.isEmpty() && !query.ids.contains(res)) {
                continue;
            }

            resultProvider->add(readAccountFromConfig(res, type));
        }
        if (query.liveQuery) {
            QObject::connect(sConfig, &ConfigNotifier::modified, guard, [resultProvider](const Sink::ApplicationDomain::SinkAccount::Ptr &account) {
                resultProvider->modify(account);
            });
            QObject::connect(sConfig, &ConfigNotifier::added, guard, [resultProvider](const Sink::ApplicationDomain::SinkAccount::Ptr &account) {
                resultProvider->add(account);
            });
            QObject::connect(sConfig, &ConfigNotifier::removed, guard,[resultProvider](const Sink::ApplicationDomain::SinkAccount::Ptr &account) {
                resultProvider->remove(account);
            });
        }
        // TODO initialResultSetComplete should be implicit
        resultProvider->initialResultSetComplete(Sink::ApplicationDomain::SinkAccount::Ptr());
        resultProvider->complete();
    });
    return qMakePair(job, emitter);
}
