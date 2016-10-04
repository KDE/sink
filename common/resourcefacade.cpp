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
#include "store.h"
#include "resourceaccess.h"
#include <QDir>

using namespace Sink;

SINK_DEBUG_AREA("ResourceFacade")

template<typename DomainType>
ConfigNotifier LocalStorageFacade<DomainType>::sConfigNotifier;

template <typename DomainType>
static typename DomainType::Ptr readFromConfig(ConfigStore &configStore, const QByteArray &id, const QByteArray &type)
{
    auto object = DomainType::Ptr::create(id);
    object->setProperty(ApplicationDomain::SinkResource::ResourceType::name, type);
    const auto configurationValues = configStore.get(id);
    for (auto it = configurationValues.constBegin(); it != configurationValues.constEnd(); it++) {
        object->setProperty(it.key(), it.value());
    }
    return object;
}

static bool matchesFilter(const QHash<QByteArray, Query::Comparator> &filter, const QMap<QByteArray, QVariant> &properties)
{
    for (const auto &filterProperty : filter.keys()) {
        if (filterProperty == ApplicationDomain::SinkResource::ResourceType::name) {
            continue;
        }
        if (!filter.value(filterProperty).matches(properties.value(filterProperty))) {
            return false;
        }
    }
    return true;
}

template<typename DomainType>
LocalStorageQueryRunner<DomainType>::LocalStorageQueryRunner(const Query &query, const QByteArray &identifier, const QByteArray &typeName, ConfigNotifier &configNotifier)
    : mResultProvider(new ResultProvider<typename DomainType::Ptr>), mConfigStore(identifier, typeName), mGuard(new QObject)
{
    QObject *guard = new QObject;
    mResultProvider->setFetcher([this, query, guard, &configNotifier](const QSharedPointer<DomainType> &) {
        const auto entries = mConfigStore.getEntries();
        for (const auto &res : entries.keys()) {
            const auto type = entries.value(res);

            if (query.hasFilter(ApplicationDomain::SinkResource::ResourceType::name) && query.getFilter(ApplicationDomain::SinkResource::ResourceType::name).value.toByteArray() != type) {
                SinkTrace() << "Skipping due to type.";
                continue;
            }
            if (!query.ids().isEmpty() && !query.ids().contains(res)) {
                continue;
            }
            const auto configurationValues = mConfigStore.get(res);
            if (!matchesFilter(query.getBaseFilters(), configurationValues)){
                SinkTrace() << "Skipping due to filter.";
                continue;
            }
            SinkTrace() << "Found match " << res;
            auto entity = readFromConfig<DomainType>(mConfigStore, res, type);
            updateStatus(*entity);
            mResultProvider->add(entity);
        }
        // TODO initialResultSetComplete should be implicit
        mResultProvider->initialResultSetComplete(typename DomainType::Ptr());
        mResultProvider->complete();
    });
    if (query.liveQuery) {
        {
            auto ret = QObject::connect(&configNotifier, &ConfigNotifier::added, guard, [this](const ApplicationDomain::ApplicationDomainType::Ptr &entry) {
                auto entity = entry.staticCast<DomainType>();
                SinkTrace() << "A new resource has been added: " << entity->identifier();
                updateStatus(*entity);
                mResultProvider->add(entity);
            });
            Q_ASSERT(ret);
        }
        {
            auto ret = QObject::connect(&configNotifier, &ConfigNotifier::modified, guard, [this](const ApplicationDomain::ApplicationDomainType::Ptr &entry) {
                auto entity = entry.staticCast<DomainType>();
                updateStatus(*entity);
                mResultProvider->modify(entity);
            });
            Q_ASSERT(ret);
        }
        {
            auto ret = QObject::connect(&configNotifier, &ConfigNotifier::removed, guard, [this](const ApplicationDomain::ApplicationDomainType::Ptr &entry) {
                mResultProvider->remove(entry.staticCast<DomainType>());
            });
            Q_ASSERT(ret);
        }
    }
    mResultProvider->onDone([=]() { delete guard; delete this; });
}

template<typename DomainType>
QObject *LocalStorageQueryRunner<DomainType>::guard() const
{
    return mGuard.get();
}

template<typename DomainType>
void LocalStorageQueryRunner<DomainType>::updateStatus(DomainType &entity)
{
    if (mStatusUpdater) {
        mStatusUpdater(entity);
    }
}

template<typename DomainType>
void LocalStorageQueryRunner<DomainType>::setStatusUpdater(const std::function<void(DomainType &)> &updater)
{
    mStatusUpdater = updater;
}

template<typename DomainType>
void LocalStorageQueryRunner<DomainType>::statusChanged(const QByteArray &identifier)
{
    SinkTrace() << "Status changed " << identifier;
    auto entity = readFromConfig<DomainType>(mConfigStore, identifier, ApplicationDomain::getTypeName<DomainType>());
    updateStatus(*entity);
    mResultProvider->modify(entity);
}

template<typename DomainType>
typename Sink::ResultEmitter<typename DomainType::Ptr>::Ptr LocalStorageQueryRunner<DomainType>::emitter()
{
    return mResultProvider->emitter();
}


template <typename DomainType>
LocalStorageFacade<DomainType>::LocalStorageFacade(const QByteArray &identifier, const QByteArray &typeName) : StoreFacade<DomainType>(), mIdentifier(identifier), mTypeName(typeName)
{
}

template <typename DomainType>
LocalStorageFacade<DomainType>::~LocalStorageFacade()
{
}

template <typename DomainType>
KAsync::Job<void> LocalStorageFacade<DomainType>::create(const DomainType &domainObject)
{
    auto configStoreIdentifier = mIdentifier;
    auto typeName = mTypeName;
    return KAsync::syncStart<void>([domainObject, configStoreIdentifier, typeName]() {
        const QByteArray type = domainObject.getProperty(typeName).toByteArray();
        const QByteArray providedIdentifier = domainObject.identifier().isEmpty() ? domainObject.getProperty("identifier").toByteArray() : domainObject.identifier();
        const QByteArray identifier = providedIdentifier.isEmpty() ? ResourceConfig::newIdentifier(type) : providedIdentifier;
        auto configStore = ConfigStore(configStoreIdentifier, typeName);
        configStore.add(identifier, type);
        auto changedProperties = domainObject.changedProperties();
        changedProperties.removeOne("identifier");
        changedProperties.removeOne(typeName);
        if (!changedProperties.isEmpty()) {
            // We have some configuration values
            QMap<QByteArray, QVariant> configurationValues;
            for (const auto &property : changedProperties) {
                configurationValues.insert(property, domainObject.getProperty(property));
            }
            configStore.modify(identifier, configurationValues);
        }
        sConfigNotifier.add(::readFromConfig<DomainType>(configStore, identifier, type));
    });
}

template <typename DomainType>
KAsync::Job<void> LocalStorageFacade<DomainType>::modify(const DomainType &domainObject)
{
    auto configStoreIdentifier = mIdentifier;
    auto typeName = mTypeName;
    return KAsync::syncStart<void>([domainObject, configStoreIdentifier, typeName]() {
        const QByteArray identifier = domainObject.identifier();
        if (identifier.isEmpty()) {
            SinkWarning() << "We need an \"identifier\" property to identify the entity to configure.";
            return;
        }
        auto changedProperties = domainObject.changedProperties();
        changedProperties.removeOne("identifier");
        changedProperties.removeOne(typeName);
        auto configStore = ConfigStore(configStoreIdentifier, typeName);
        if (!changedProperties.isEmpty()) {
            // We have some configuration values
            QMap<QByteArray, QVariant> configurationValues;
            for (const auto &property : changedProperties) {
                configurationValues.insert(property, domainObject.getProperty(property));
            }
            configStore.modify(identifier, configurationValues);
        }

        const auto type = configStore.getEntries().value(identifier);
        sConfigNotifier.modify(::readFromConfig<DomainType>(configStore, identifier, type));
    });
}

template <typename DomainType>
KAsync::Job<void> LocalStorageFacade<DomainType>::remove(const DomainType &domainObject)
{
    auto configStoreIdentifier = mIdentifier;
    auto typeName = mTypeName;
    return KAsync::syncStart<void>([domainObject, configStoreIdentifier, typeName]() {
        const QByteArray identifier = domainObject.identifier();
        if (identifier.isEmpty()) {
            SinkWarning() << "We need an \"identifier\" property to identify the entity to configure";
            return;
        }
        SinkTrace() << "Removing: " << identifier;
        auto configStore = ConfigStore(configStoreIdentifier, typeName);
        configStore.remove(identifier);
        sConfigNotifier.remove(QSharedPointer<DomainType>::create(domainObject));
    });
}

template <typename DomainType>
QPair<KAsync::Job<void>, typename ResultEmitter<typename DomainType::Ptr>::Ptr> LocalStorageFacade<DomainType>::load(const Query &query)
{
    auto runner = new LocalStorageQueryRunner<DomainType>(query, mIdentifier, mTypeName, sConfigNotifier);
    return qMakePair(KAsync::null<void>(), runner->emitter());
}

ResourceFacade::ResourceFacade() : LocalStorageFacade<Sink::ApplicationDomain::SinkResource>("resources", Sink::ApplicationDomain::SinkResource::ResourceType::name)
{
}

ResourceFacade::~ResourceFacade()
{
}

KAsync::Job<void> ResourceFacade::remove(const Sink::ApplicationDomain::SinkResource &resource)
{
    const auto identifier = resource.identifier();
    return Sink::Store::removeDataFromDisk(identifier).then(LocalStorageFacade<Sink::ApplicationDomain::SinkResource>::remove(resource));
}

QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename ApplicationDomain::SinkResource::Ptr>::Ptr> ResourceFacade::load(const Sink::Query &query)
{
    auto runner = new LocalStorageQueryRunner<ApplicationDomain::SinkResource>(query, mIdentifier, mTypeName, sConfigNotifier);
    auto monitoredResources = QSharedPointer<QSet<QByteArray>>::create();
    runner->setStatusUpdater([runner, monitoredResources](ApplicationDomain::SinkResource &resource) {
        auto resourceAccess = ResourceAccessFactory::instance().getAccess(resource.identifier(), ResourceConfig::getResourceType(resource.identifier()));
        if (!monitoredResources->contains(resource.identifier())) {
            auto ret = QObject::connect(resourceAccess.data(), &ResourceAccess::notification, runner->guard(), [resource, runner, resourceAccess](const Notification &notification) {
                SinkTrace() << "Received notification in facade: " << notification.type;
                if (notification.type == Notification::Status) {
                    runner->statusChanged(resource.identifier());
                }
            });
            Q_ASSERT(ret);
            monitoredResources->insert(resource.identifier());
        }
        resource.setStatusStatus(resourceAccess->getResourceStatus());
    });
    return qMakePair(KAsync::null<void>(), runner->emitter());
}


AccountFacade::AccountFacade() : LocalStorageFacade<Sink::ApplicationDomain::SinkAccount>("accounts", ApplicationDomain::SinkAccount::AccountType::name)
{
}

AccountFacade::~AccountFacade()
{
}

QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename ApplicationDomain::SinkAccount::Ptr>::Ptr> AccountFacade::load(const Sink::Query &query)
{
    auto runner = new LocalStorageQueryRunner<ApplicationDomain::SinkAccount>(query, mIdentifier, mTypeName, sConfigNotifier);
    auto monitoredResources = QSharedPointer<QSet<QByteArray>>::create();
    runner->setStatusUpdater([runner, monitoredResources](ApplicationDomain::SinkAccount &account) {
        Query query;
        query.filter<ApplicationDomain::SinkResource::Account>(account.identifier());
        const auto resources = Store::read<ApplicationDomain::SinkResource>(query);
        SinkTrace() << "Found resource belonging to the account " << account.identifier() << " : " << resources;
        auto accountIdentifier = account.identifier();
        ApplicationDomain::Status status = ApplicationDomain::ConnectedStatus;
        for (const auto &resource : resources) {
            auto resourceAccess = ResourceAccessFactory::instance().getAccess(resource.identifier(), ResourceConfig::getResourceType(resource.identifier()));
            if (!monitoredResources->contains(resource.identifier())) {
                auto ret = QObject::connect(resourceAccess.data(), &ResourceAccess::notification, runner->guard(), [resource, runner, resourceAccess, accountIdentifier](const Notification &notification) {
                    SinkTrace() << "Received notification in facade: " << notification.type;
                    if (notification.type == Notification::Status) {
                        runner->statusChanged(accountIdentifier);
                    }
                });
                Q_ASSERT(ret);
                monitoredResources->insert(resource.identifier());
            }

            //Figure out overall status
            auto s = resourceAccess->getResourceStatus();
            switch (s) {
                case ApplicationDomain::ErrorStatus:
                    status = ApplicationDomain::ErrorStatus;
                    break;
                case ApplicationDomain::OfflineStatus:
                    if (status == ApplicationDomain::ConnectedStatus) {
                        status = ApplicationDomain::OfflineStatus;
                    }
                    break;
                case ApplicationDomain::ConnectedStatus:
                    break;
                case ApplicationDomain::BusyStatus:
                    if (status != ApplicationDomain::ErrorStatus) {
                        status = ApplicationDomain::BusyStatus;
                    }
                    break;
            }
        }
        account.setStatusStatus(status);
    });
    return qMakePair(KAsync::null<void>(), runner->emitter());
}

IdentityFacade::IdentityFacade() : LocalStorageFacade<Sink::ApplicationDomain::Identity>("identities", "type")
{
}

IdentityFacade::~IdentityFacade()
{
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "moc_resourcefacade.cpp"
#pragma clang diagnostic pop
