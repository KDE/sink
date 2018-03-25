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
#include "store.h"
#include "resourceaccess.h"
#include "resource.h"
#include "facadefactory.h"

using namespace Sink;

template<typename DomainType>
ConfigNotifier LocalStorageFacade<DomainType>::sConfigNotifier;

static void applyConfig(ConfigStore &configStore, const QByteArray &id, ApplicationDomain::ApplicationDomainType &object, const QByteArrayList &requestedProperties)
{
    const auto configurationValues = configStore.get(id);
    for (auto it = configurationValues.constBegin(); it != configurationValues.constEnd(); it++) {
        object.setProperty(it.key(), it.value());
    }
    //Populate the object with dummy values for non-available but requested properties.
    //This avoid a warning about non-existing properties in bufferadaptor.h
    if (!requestedProperties.isEmpty()) {
        for (const auto &requested: requestedProperties) {
            if (!object.hasProperty(requested)) {
                object.setProperty(requested, QVariant{});
            }
        }
    }
}

template <typename DomainType>
static typename DomainType::Ptr readFromConfig(ConfigStore &configStore, const QByteArray &id, const QByteArray &type, const QByteArrayList &requestedProperties)
{
    auto object = DomainType::Ptr::create(id);
    applyConfig(configStore, id, *object, requestedProperties);
    return object;
}

template <>
typename ApplicationDomain::SinkAccount::Ptr readFromConfig<ApplicationDomain::SinkAccount>(ConfigStore &configStore, const QByteArray &id, const QByteArray &type, const QByteArrayList &requestedProperties)
{
    auto object = ApplicationDomain::SinkAccount::Ptr::create(id);
    object->setProperty(ApplicationDomain::SinkAccount::AccountType::name, type);
    applyConfig(configStore, id, *object, requestedProperties);
    return object;
}

template <>
typename ApplicationDomain::SinkResource::Ptr readFromConfig<ApplicationDomain::SinkResource>(ConfigStore &configStore, const QByteArray &id, const QByteArray &type, const QByteArrayList &requestedProperties)
{
    auto object = ApplicationDomain::SinkResource::Ptr::create(id);
    object->setProperty(ApplicationDomain::SinkResource::ResourceType::name, type);
    //Apply the capabilities where we have capabilities
    if (!ApplicationDomain::isGlobalType(type)) {
        if (auto res = ResourceFactory::load(type)) {
            object->setCapabilities(res->capabilities());
        }
    }
    applyConfig(configStore, id, *object, requestedProperties);
    return object;
}

static bool matchesFilter(const QHash<QByteArray, Query::Comparator> &filter, const ApplicationDomain::ApplicationDomainType &entity)
{
    for (const auto &filterProperty : filter.keys()) {
        if (filterProperty == ApplicationDomain::SinkResource::ResourceType::name) {
            continue;
        }
        if (!filter.value(filterProperty).matches(entity.getProperty(filterProperty))) {
            return false;
        }
    }
    return true;
}

template<typename DomainType>
LocalStorageQueryRunner<DomainType>::LocalStorageQueryRunner(const Query &query, const QByteArray &identifier, const QByteArray &typeName, ConfigNotifier &configNotifier, const Sink::Log::Context &ctx)
    : mResultProvider(new ResultProvider<typename DomainType::Ptr>), mConfigStore(identifier, typeName), mGuard(new QObject), mLogCtx(ctx.subContext("config"))
{

    auto matchesTypeAndIds = [query, this] (const QByteArray &type, const QByteArray &id) {
        if (query.hasFilter(ApplicationDomain::SinkResource::ResourceType::name) && query.getFilter(ApplicationDomain::SinkResource::ResourceType::name).value.toByteArray() != type) {
            SinkTraceCtx(mLogCtx) << "Skipping due to type.";
            return false;
        }
        if (!query.ids().isEmpty() && !query.ids().contains(id)) {
            return false;
        }
        return true;
    };

    QObject *guard = new QObject;
    mResultProvider->setFetcher([this, query, matchesTypeAndIds]() {
        const auto entries = mConfigStore.getEntries();
        for (const auto &res : entries.keys()) {
            const auto type = entries.value(res);
            if (!matchesTypeAndIds(type, res)){
                continue;
            }
            auto entity = readFromConfig<DomainType>(mConfigStore, res, type, query.requestedProperties);
            if (!matchesFilter(query.getBaseFilters(), *entity)){
                SinkTraceCtx(mLogCtx) << "Skipping due to filter." << res;
                continue;
            }
            SinkTraceCtx(mLogCtx) << "Found match " << res;
            updateStatus(*entity);
            mResultProvider->add(entity);
        }
        // TODO initialResultSetComplete should be implicit
        mResultProvider->initialResultSetComplete(true);
        mResultProvider->complete();
    });
    if (query.liveQuery()) {
        {
            auto ret = QObject::connect(&configNotifier, &ConfigNotifier::added, guard, [this, query, matchesTypeAndIds](const ApplicationDomain::ApplicationDomainType::Ptr &entry, const QByteArray &type) {
                auto entity = entry.staticCast<DomainType>();
                if (!matchesTypeAndIds(type, entity->identifier())){
                    return;
                }
                if (!matchesFilter(query.getBaseFilters(), *entity)){
                    return;
                }
                SinkTraceCtx(mLogCtx) << "A new resource has been added: " << entity->identifier();
                updateStatus(*entity);
                mResultProvider->add(entity);
            });
            Q_ASSERT(ret);
        }
        {
            auto ret = QObject::connect(&configNotifier, &ConfigNotifier::modified, guard, [this, query, matchesTypeAndIds](const ApplicationDomain::ApplicationDomainType::Ptr &entry, const QByteArray &type) {
                auto entity = entry.staticCast<DomainType>();
                if (!matchesTypeAndIds(type, entity->identifier())){
                    return;
                }
                if (!matchesFilter(query.getBaseFilters(), *entity)){
                    return;
                }
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
    SinkTraceCtx(mLogCtx) << "Status changed " << identifier;
    auto entity = readFromConfig<DomainType>(mConfigStore, identifier, ApplicationDomain::getTypeName<DomainType>(), QByteArrayList{});
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
    return KAsync::start([domainObject, configStoreIdentifier, typeName]() {
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
        sConfigNotifier.add(::readFromConfig<DomainType>(configStore, identifier, type, QByteArrayList{}), type);
    });
}

template <typename DomainType>
KAsync::Job<void> LocalStorageFacade<DomainType>::modify(const DomainType &domainObject)
{
    auto configStoreIdentifier = mIdentifier;
    auto typeName = mTypeName;
    return KAsync::start([domainObject, configStoreIdentifier, typeName]() {
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
        sConfigNotifier.modify(::readFromConfig<DomainType>(configStore, identifier, type, QByteArrayList{}), type);
    });
}

template <typename DomainType>
KAsync::Job<void> LocalStorageFacade<DomainType>::move(const DomainType &, const QByteArray &)
{
    return KAsync::error<void>(1, "Resources and Accounts cannot be moved.");
}

template <typename DomainType>
KAsync::Job<void> LocalStorageFacade<DomainType>::copy(const DomainType &, const QByteArray &)
{
    return KAsync::error<void>(1, "Resources and Accounts cannot be copied.");
}

template <typename DomainType>
KAsync::Job<void> LocalStorageFacade<DomainType>::remove(const DomainType &domainObject)
{
    auto configStoreIdentifier = mIdentifier;
    auto typeName = mTypeName;
    return KAsync::start([domainObject, configStoreIdentifier, typeName]() {
        const QByteArray identifier = domainObject.identifier();
        if (identifier.isEmpty()) {
            SinkWarning() << "We need an \"identifier\" property to identify the entity to configure";
            return;
        }
        SinkTrace() << "Removing: " << identifier;
        auto configStore = ConfigStore(configStoreIdentifier, typeName);
        configStore.remove(identifier);
        sConfigNotifier.remove(QSharedPointer<DomainType>::create(domainObject), typeName);
    });
}

template <typename DomainType>
QPair<KAsync::Job<void>, typename ResultEmitter<typename DomainType::Ptr>::Ptr> LocalStorageFacade<DomainType>::load(const Query &query, const Sink::Log::Context &parentCtx)
{
    auto ctx = parentCtx.subContext(ApplicationDomain::getTypeName<DomainType>());
    auto runner = new LocalStorageQueryRunner<DomainType>(query, mIdentifier, mTypeName, sConfigNotifier, ctx);
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

QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename ApplicationDomain::SinkResource::Ptr>::Ptr> ResourceFacade::load(const Sink::Query &query, const Sink::Log::Context &parentCtx)
{
    auto ctx = parentCtx.subContext("resource");
    auto runner = new LocalStorageQueryRunner<ApplicationDomain::SinkResource>(query, mIdentifier, mTypeName, sConfigNotifier, ctx);
    auto monitoredResources = QSharedPointer<QSet<QByteArray>>::create();
    runner->setStatusUpdater([runner, monitoredResources, ctx](ApplicationDomain::SinkResource &resource) {
        auto resourceAccess = ResourceAccessFactory::instance().getAccess(resource.identifier(), ResourceConfig::getResourceType(resource.identifier()));
        if (!monitoredResources->contains(resource.identifier())) {
            auto ret = QObject::connect(resourceAccess.data(), &ResourceAccess::notification, runner->guard(), [resource, runner, resourceAccess, ctx](const Notification &notification) {
                SinkTraceCtx(ctx) << "Received notification in facade: " << notification.type;
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

QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename ApplicationDomain::SinkAccount::Ptr>::Ptr> AccountFacade::load(const Sink::Query &query, const Sink::Log::Context &parentCtx)
{
    auto ctx = parentCtx.subContext("accounts");
    auto runner = new LocalStorageQueryRunner<ApplicationDomain::SinkAccount>(query, mIdentifier, mTypeName, sConfigNotifier, ctx);
    auto monitoredResources = QSharedPointer<QSet<QByteArray>>::create();
    auto monitorResource = [monitoredResources, runner, ctx] (const QByteArray &accountIdentifier, const ApplicationDomain::SinkResource &resource, const ResourceAccess::Ptr &resourceAccess) {
        if (!monitoredResources->contains(resource.identifier())) {
            auto ret = QObject::connect(resourceAccess.data(), &ResourceAccess::notification, runner->guard(), [resource, runner, resourceAccess, accountIdentifier, ctx](const Notification &notification) {
                SinkTraceCtx(ctx) << "Received notification in facade: " << notification.type;
                if (notification.type == Notification::Status) {
                    runner->statusChanged(accountIdentifier);
                }
            });
            Q_ASSERT(ret);
            monitoredResources->insert(resource.identifier());
        }
    };
    runner->setStatusUpdater([runner, monitoredResources, ctx, monitorResource](ApplicationDomain::SinkAccount &account) {
        Query query{Query::LiveQuery};
        query.filter<ApplicationDomain::SinkResource::Account>(account.identifier());
        query.request<ApplicationDomain::SinkResource::Account>()
             .request<ApplicationDomain::SinkResource::Capabilities>();
        const auto resources = Store::read<ApplicationDomain::SinkResource>(query);
        SinkTraceCtx(ctx) << "Found resource belonging to the account " << account.identifier() << " : " << resources;
        auto accountIdentifier = account.identifier();

        //Monitor for new resources so they can be monitored as well
        if (!runner->mResourceEmitter.contains(accountIdentifier)) {
            auto facade = Sink::FacadeFactory::instance().getFacade<ApplicationDomain::SinkResource>();
            Q_ASSERT(facade);

            auto emitter = facade->load(query, ctx).second;
            emitter->onAdded([=](const ApplicationDomain::SinkResource::Ptr &resource) {
                auto resourceAccess = Sink::ResourceAccessFactory::instance().getAccess(resource->identifier(), ResourceConfig::getResourceType(resource->identifier()));
                monitorResource(accountIdentifier, *resource, resourceAccess);
            });
            emitter->fetch();
            runner->mResourceEmitter[accountIdentifier] = emitter;
        }

        QList<int> states;
        //Gather all resources and ensure they are monitored
        for (const auto &resource : resources) {
            auto resourceAccess = ResourceAccessFactory::instance().getAccess(resource.identifier(), ResourceConfig::getResourceType(resource.identifier()));
            monitorResource(accountIdentifier, resource, resourceAccess);
            states << resourceAccess->getResourceStatus();
        }
        const auto status = [&] {
            if (states.contains(ApplicationDomain::ErrorStatus)) {
                return ApplicationDomain::ErrorStatus;
            }
            if (states.contains(ApplicationDomain::BusyStatus)) {
                return ApplicationDomain::BusyStatus;
            }
            if (states.contains(ApplicationDomain::OfflineStatus)) {
                return ApplicationDomain::OfflineStatus;
            }
            if (states.contains(ApplicationDomain::ConnectedStatus)) {
                return ApplicationDomain::ConnectedStatus;
            }
            return ApplicationDomain::NoStatus;
        }();
        account.setStatusStatus(status);
    });
    return qMakePair(KAsync::null<void>(), runner->emitter());
}

KAsync::Job<void> AccountFacade::remove(const Sink::ApplicationDomain::SinkAccount &account)
{
    using namespace Sink::ApplicationDomain;
    auto job = KAsync::null();

    //Remove all resources
    job = job.then(Store::fetch<SinkResource>(Sink::Query{}.filter<SinkResource::Account>(account)))
        .each([] (const SinkResource::Ptr &resource) { return Store::remove(*resource); });
    //Remove all identities
    job = job.then(Store::fetch<Identity>(Sink::Query{}.filter<Identity::Account>(account)))
        .each([] (const Identity::Ptr &identity) { return Store::remove(*identity); });
 
    return job.then(LocalStorageFacade<Sink::ApplicationDomain::SinkAccount>::remove(account));
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
