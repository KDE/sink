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

template <typename DomainType>
ConfigNotifier LocalStorageFacade<DomainType>::sConfigNotifier;

template <typename DomainType>
LocalStorageFacade<DomainType>::LocalStorageFacade(const QByteArray &identifier) : Sink::StoreFacade<DomainType>(), mConfigStore(identifier)
{
}

template <typename DomainType>
LocalStorageFacade<DomainType>::~LocalStorageFacade()
{
}

template <typename DomainType>
typename DomainType::Ptr LocalStorageFacade<DomainType>::readFromConfig(const QByteArray &id, const QByteArray &type)
{
    auto object = DomainType::Ptr::create(id);
    object->setProperty("type", type);
    const auto configurationValues = mConfigStore.get(id);
    for (auto it = configurationValues.constBegin(); it != configurationValues.constEnd(); it++) {
        object->setProperty(it.key(), it.value());
    }
    return object;
}

template <typename DomainType>
KAsync::Job<void> LocalStorageFacade<DomainType>::create(const DomainType &domainObject)
{
    return KAsync::start<void>([domainObject, this]() {
        const QByteArray type = domainObject.getProperty("type").toByteArray();
        //FIXME use .identifier() instead
        const QByteArray providedIdentifier = domainObject.getProperty("identifier").toByteArray();
        const QByteArray identifier = providedIdentifier.isEmpty() ? ResourceConfig::newIdentifier(type) : providedIdentifier;
        mConfigStore.add(identifier, type);
        auto changedProperties = domainObject.changedProperties();
        changedProperties.removeOne("identifier");
        changedProperties.removeOne("type");
        if (!changedProperties.isEmpty()) {
            // We have some configuration values
            QMap<QByteArray, QVariant> configurationValues;
            for (const auto &property : changedProperties) {
                configurationValues.insert(property, domainObject.getProperty(property));
            }
            mConfigStore.modify(identifier, configurationValues);
        }
        sConfigNotifier.add(readFromConfig(identifier, type));
    });
}

template <typename DomainType>
KAsync::Job<void> LocalStorageFacade<DomainType>::modify(const DomainType &domainObject)
{
    return KAsync::start<void>([domainObject, this]() {
        const QByteArray identifier = domainObject.identifier();
        if (identifier.isEmpty()) {
            Warning() << "We need an \"identifier\" property to identify the entity to configure.";
            return;
        }
        auto changedProperties = domainObject.changedProperties();
        changedProperties.removeOne("identifier");
        changedProperties.removeOne("type");
        if (!changedProperties.isEmpty()) {
            // We have some configuration values
            QMap<QByteArray, QVariant> configurationValues;
            for (const auto &property : changedProperties) {
                configurationValues.insert(property, domainObject.getProperty(property));
            }
            mConfigStore.modify(identifier, configurationValues);
        }

        const auto type = mConfigStore.getEntries().value(identifier);
        sConfigNotifier.modify(readFromConfig(identifier, type));
    });
}

template <typename DomainType>
KAsync::Job<void> LocalStorageFacade<DomainType>::remove(const DomainType &domainObject)
{
    return KAsync::start<void>([domainObject, this]() {
        const QByteArray identifier = domainObject.identifier();
        if (identifier.isEmpty()) {
            Warning() << "We need an \"identifier\" property to identify the entity to configure";
            return;
        }
        mConfigStore.remove(identifier);
        sConfigNotifier.remove(QSharedPointer<DomainType>::create(domainObject));
    });
}

template <typename DomainType>
QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename DomainType::Ptr>::Ptr> LocalStorageFacade<DomainType>::load(const Sink::Query &query)
{
    QObject *guard = new QObject;
    auto resultProvider = new Sink::ResultProvider<typename DomainType::Ptr>();
    auto emitter = resultProvider->emitter();
    resultProvider->setFetcher([](const QSharedPointer<DomainType> &) {});
    resultProvider->onDone([=]() { delete resultProvider; delete guard; });
    auto job = KAsync::start<void>([=]() {
        const auto entries = mConfigStore.getEntries();
        for (const auto &res : entries.keys()) {
            const auto type = entries.value(res);
            if (!query.ids.isEmpty() && !query.ids.contains(res)) {
                continue;
            }
            resultProvider->add(readFromConfig(res, type));
        }
        if (query.liveQuery) {
            QObject::connect(&sConfigNotifier, &ConfigNotifier::modified, guard, [resultProvider](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &entry) {
                resultProvider->modify(entry.staticCast<DomainType>());
            });
            QObject::connect(&sConfigNotifier, &ConfigNotifier::added, guard, [resultProvider](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &entry) {
                resultProvider->add(entry.staticCast<DomainType>());
            });
            QObject::connect(&sConfigNotifier, &ConfigNotifier::removed, guard,[resultProvider](const Sink::ApplicationDomain::ApplicationDomainType::Ptr &entry) {
                resultProvider->remove(entry.staticCast<DomainType>());
            });
        }
        // TODO initialResultSetComplete should be implicit
        resultProvider->initialResultSetComplete(typename DomainType::Ptr());
        resultProvider->complete();
    });
    return qMakePair(job, emitter);
}







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
        Trace() << "Creating resource " << type << identifier;
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
            auto config = ResourceConfig::getConfiguration(identifier);
            for (const auto &property : changedProperties) {
                config.insert(property, resource.getProperty(property));
            }
            ResourceConfig::configureResource(identifier, config);
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
        Trace() << "Removing resource " << identifier;
        ResourceConfig::removeResource(identifier);
        // TODO shutdown resource, or use the resource process with a --remove option to cleanup (so we can take advantage of the file locking)
        QDir dir(Sink::storageLocation());
        for (const auto &folder : dir.entryList(QStringList() << identifier + "*")) {
            Sink::Storage(Sink::storageLocation(), folder, Sink::Storage::ReadWrite).removeFromDisk();
        }
    });
}

static bool matchesFilter(const QHash<QByteArray, QVariant> &filter, const QMap<QByteArray, QVariant> &properties)
{
    for (const auto &filterProperty : filter.keys()) {
        if (filterProperty == "type") {
            continue;
        }
        if (filter.value(filterProperty).toByteArray() != properties.value(filterProperty).toByteArray()) {
            return false;
        }
    }
    return true;
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
            const auto configurationValues = ResourceConfig::getConfiguration(res);
            if (!matchesFilter(query.propertyFilter, configurationValues)){
                continue;
            }

            auto resource = Sink::ApplicationDomain::SinkResource::Ptr::create(res);
            resource->setProperty("type", type);
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



AccountFacade::AccountFacade(const QByteArray &) : LocalStorageFacade<Sink::ApplicationDomain::SinkAccount>("accounts")
{
}

AccountFacade::~AccountFacade()
{
}

