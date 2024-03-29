/*
 * Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "store.h"

#include <QTime>
#include <QAbstractItemModel>
#include <functional>
#include <memory>

#include "resourceaccess.h"
#include "commands.h"
#include "resourcefacade.h"
#include "definitions.h"
#include "resourceconfig.h"
#include "resourcecontrol.h"
#include "facadefactory.h"
#include "modelresult.h"
#include "storage.h"
#include "log.h"
#include "utils.h"

#define ASSERT_ENUMS_MATCH(A, B) Q_STATIC_ASSERT_X(static_cast<int>(A) == static_cast<int>(B), "The enum values must match");

//Ensure the copied enum matches
typedef ModelResult<Sink::ApplicationDomain::Mail, Sink::ApplicationDomain::Mail::Ptr> MailModelResult;
ASSERT_ENUMS_MATCH(Sink::Store::DomainObjectBaseRole, MailModelResult::DomainObjectBaseRole)
ASSERT_ENUMS_MATCH(Sink::Store::ChildrenFetchedRole, MailModelResult::ChildrenFetchedRole)
ASSERT_ENUMS_MATCH(Sink::Store::DomainObjectRole, MailModelResult::DomainObjectRole)
ASSERT_ENUMS_MATCH(Sink::Store::StatusRole, MailModelResult::StatusRole)
ASSERT_ENUMS_MATCH(Sink::Store::WarningRole, MailModelResult::WarningRole)
ASSERT_ENUMS_MATCH(Sink::Store::ProgressRole, MailModelResult::ProgressRole)

Q_DECLARE_METATYPE(QSharedPointer<Sink::ResultEmitter<Sink::ApplicationDomain::SinkResource::Ptr>>)
Q_DECLARE_METATYPE(QSharedPointer<Sink::ResourceAccessInterface>);
Q_DECLARE_METATYPE(std::shared_ptr<void>);


static bool sanityCheckQuery(const Sink::Query &query)
{
    for (const auto &id : query.ids()) {
        if (id.isEmpty()) {
            SinkError() << "Empty id in query.";
            return false;
        }
    }
    return true;
}

static KAsync::Job<void> forEachResource(const Sink::SyncScope &scope, std::function<KAsync::Job<void>(const Sink::ApplicationDomain::SinkResource::Ptr &resource)> callback)
{
    using namespace Sink;
    auto resourceFilter = scope.getResourceFilter();
    //Filter resources by type by default
    if (!resourceFilter.propertyFilter.contains({ApplicationDomain::SinkResource::Capabilities::name}) && !scope.type().isEmpty()) {
        resourceFilter.propertyFilter.insert({ApplicationDomain::SinkResource::Capabilities::name}, Query::Comparator{scope.type(), Query::Comparator::Contains});
    }
    Sink::Query query;
    query.setFilter(resourceFilter);
    return Store::fetchAll<ApplicationDomain::SinkResource>(query)
        .template each(callback);
}

namespace Sink {

QString Store::storageLocation()
{
    return Sink::storageLocation();
}


template <class DomainType>
KAsync::Job<void> queryResource(const QByteArray resourceType, const QByteArray &resourceInstanceIdentifier, const Query &query, typename AggregatingResultEmitter<typename DomainType::Ptr>::Ptr aggregatingEmitter, const Sink::Log::Context &ctx_)
{
    auto ctx = ctx_.subContext(resourceInstanceIdentifier);
    auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceType, resourceInstanceIdentifier);
    if (facade) {
        SinkTraceCtx(ctx) << "Trying to fetch from resource " << resourceInstanceIdentifier;
        auto result = facade->load(query, ctx);
        if (result.second) {
            aggregatingEmitter->addEmitter(result.second);
        } else {
            SinkWarningCtx(ctx) << "Null emitter for resource " << resourceInstanceIdentifier;
        }
        return result.first;
    } else {
        SinkTraceCtx(ctx) << "Couldn' find a facade for " << resourceInstanceIdentifier;
        // Ignore the error and carry on
        return KAsync::null<void>();
    }
}

template <class DomainType>
QPair<typename AggregatingResultEmitter<typename DomainType::Ptr>::Ptr,  typename ResultEmitter<typename ApplicationDomain::SinkResource::Ptr>::Ptr> getEmitter(Query query, const Log::Context &ctx)
{
    query.setType(ApplicationDomain::getTypeName<DomainType>());
    SinkTraceCtx(ctx) << "Query: " << query;

    // Query all resources and aggregate results
    auto aggregatingEmitter = AggregatingResultEmitter<typename DomainType::Ptr>::Ptr::create();
    if (ApplicationDomain::isGlobalType(ApplicationDomain::getTypeName<DomainType>())) {
        //For global types we don't need to query for the resources first.
        queryResource<DomainType>("", "", query, aggregatingEmitter, ctx).exec();
    } else {
        auto resourceCtx = ctx.subContext("resourceQuery");
        auto facade = FacadeFactory::instance().getFacade<ApplicationDomain::SinkResource>();
        Q_ASSERT(facade);
        Sink::Query resourceQuery;
        resourceQuery.request<ApplicationDomain::SinkResource::Capabilities>();
        if (query.liveQuery()) {
            SinkTraceCtx(ctx) << "Listening for new resources.";
            resourceQuery.setFlags(Query::LiveQuery);
        }

        //Filter resources by available content types (unless the query already specifies a capability filter)
        auto resourceFilter = query.getResourceFilter();
        if (!resourceFilter.propertyFilter.contains({ApplicationDomain::SinkResource::Capabilities::name})) {
            resourceFilter.propertyFilter.insert({ApplicationDomain::SinkResource::Capabilities::name}, Query::Comparator{ApplicationDomain::getTypeName<DomainType>(), Query::Comparator::Contains});
        }
        resourceQuery.setFilter(resourceFilter);
        for (auto const &properties :  resourceFilter.propertyFilter.keys()) {
            resourceQuery.requestedProperties << properties;
        }

        auto result = facade->load(resourceQuery, resourceCtx);
        auto emitter = result.second;
        emitter->onAdded([=](const ApplicationDomain::SinkResource::Ptr &resource) {
            SinkTraceCtx(resourceCtx) << "Found new resources: " << resource->identifier();
            const auto resourceType = ResourceConfig::getResourceType(resource->identifier());
            Q_ASSERT(!resourceType.isEmpty());
            queryResource<DomainType>(resourceType, resource->identifier(), query, aggregatingEmitter, ctx).exec();
        });
        emitter->onComplete([query, aggregatingEmitter, resourceCtx]() {
            SinkTraceCtx(resourceCtx) << "Resource query complete";
        });

        return qMakePair(aggregatingEmitter, emitter);
    }
    return qMakePair(aggregatingEmitter, ResultEmitter<typename ApplicationDomain::SinkResource::Ptr>::Ptr{});
}

static Log::Context getQueryContext(const Sink::Query &query, const QByteArray &type)
{
    if (!query.id().isEmpty()) {
        return Log::Context{"query." + type + "." + query.id()};
    }
    return Log::Context{"query." + type};
}

template <class DomainType>
QSharedPointer<QAbstractItemModel> Store::loadModel(const Query &query)
{
    Q_ASSERT(sanityCheckQuery(query));
    auto ctx = getQueryContext(query, ApplicationDomain::getTypeName<DomainType>());
    auto model = QSharedPointer<ModelResult<DomainType, typename DomainType::Ptr>>::create(query, query.requestedProperties, ctx);

    //* Client defines lifetime of model
    //* The model lifetime defines the duration of live-queries
    //* The facade needs to life for the duration of any calls being made (assuming we get rid of any internal callbacks
    //* The emitter needs to live or the duration of query (respectively, the model)
    //* The result provider needs to live for as long as results are provided (until the last thread exits).

    auto result = getEmitter<DomainType>(query, ctx);
    model->setEmitter(result.first);

    //Keep the emitter alive
    if (auto resourceEmitter = result.second) {
        model->setProperty("resourceEmitter", QVariant::fromValue(resourceEmitter)); //TODO only neceesary for live queries
        resourceEmitter->fetch();
    }


    //Automatically populate the top-level
    model->fetchMore(QModelIndex());

    return std::move(model);
}

template <class DomainType>
void Store::updateModel(const Query &query, const QSharedPointer<QAbstractItemModel> &model)
{
    Q_ASSERT(sanityCheckQuery(query));
    auto ctx = getQueryContext(query, ApplicationDomain::getTypeName<DomainType>());

    auto result = getEmitter<DomainType>(query, ctx);

    QSharedPointer<ModelResult<DomainType, typename DomainType::Ptr>> m = model.dynamicCast<ModelResult<DomainType, typename DomainType::Ptr>>();
    Q_ASSERT(m);
    m->setEmitter(result.first);

    //Keep the emitter alive
    if (auto resourceEmitter = result.second) {
        m->setProperty("resourceEmitter", QVariant::fromValue(resourceEmitter)); //TODO only neceesary for live queries
        resourceEmitter->fetch();
    }

    m->updateQuery(query);
}

template <class DomainType>
static std::shared_ptr<StoreFacade<DomainType>> getFacade(const QByteArray &resourceInstanceIdentifier)
{
    if (ApplicationDomain::isGlobalType(ApplicationDomain::getTypeName<DomainType>())) {
        if (auto facade = FacadeFactory::instance().getFacade<DomainType>()) {
            return facade;
        }
    }
    if (auto facade = FacadeFactory::instance().getFacade<DomainType>(ResourceConfig::getResourceType(resourceInstanceIdentifier), resourceInstanceIdentifier)) {
        return facade;
    }
    return std::make_shared<NullFacade<DomainType>>();
}

template <class DomainType>
KAsync::Job<void> Store::create(const DomainType &domainObject)
{
    SinkLog() << "Create: " << domainObject;
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    return facade->create(domainObject).addToContext(std::shared_ptr<void>(facade)).onError([](const KAsync::Error &error) { SinkWarning() << "Failed to create " << error; });
}

template <class DomainType>
KAsync::Job<void> Store::modify(const DomainType &domainObject)
{
    if (domainObject.changedProperties().isEmpty()) {
        SinkLog() << "Nothing to modify: " << domainObject.identifier();
        return KAsync::null();
    }
    SinkLog() << "Modify: " << domainObject;
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    if (domainObject.isAggregate()) {
        return KAsync::value(domainObject.aggregatedIds())
            .addToContext(std::shared_ptr<void>(facade))
            .each([=] (const QByteArray &id) {
                auto object = Sink::ApplicationDomain::ApplicationDomainType::createCopy(id, domainObject);
                return facade->modify(object).onError([](const KAsync::Error &error) { SinkWarning() << "Failed to modify " << error; });
        });
    }
    return facade->modify(domainObject).addToContext(std::shared_ptr<void>(facade)).onError([](const KAsync::Error &error) { SinkWarning() << "Failed to modify"; });
}

template <class DomainType>
KAsync::Job<void> Store::modify(const Query &query, const DomainType &domainObject)
{
    if (domainObject.changedProperties().isEmpty()) {
        SinkLog() << "Nothing to modify: " << domainObject.identifier();
        return KAsync::null();
    }
    SinkLog() << "Modify: " << query << domainObject;
    return fetchAll<DomainType>(query)
        .each([=] (const typename DomainType::Ptr &entity) {
            auto copy = *entity;
            for (const auto &p : domainObject.changedProperties()) {
                copy.setProperty(p, domainObject.getProperty(p));
            }
            return modify(copy);
        });
}

template <class DomainType>
KAsync::Job<void> Store::move(const DomainType &domainObject, const QByteArray &newResource)
{
    SinkLog() << "Move: " << domainObject << newResource;
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    if (domainObject.isAggregate()) {
        return KAsync::value(domainObject.aggregatedIds())
            .addToContext(std::shared_ptr<void>(facade))
            .each([=] (const QByteArray &id) {
                auto object = Sink::ApplicationDomain::ApplicationDomainType::createCopy(id, domainObject);
                return facade->move(object, newResource).onError([](const KAsync::Error &error) { SinkWarning() << "Failed to move " << error; });
        });
    }
    return facade->move(domainObject, newResource).addToContext(std::shared_ptr<void>(facade)).onError([](const KAsync::Error &error) { SinkWarning() << "Failed to move " << error; });
}

template <class DomainType>
KAsync::Job<void> Store::copy(const DomainType &domainObject, const QByteArray &newResource)
{
    SinkLog() << "Copy: " << domainObject << newResource;
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    if (domainObject.isAggregate()) {
        return KAsync::value(domainObject.aggregatedIds())
            .addToContext(std::shared_ptr<void>(facade))
            .each([=] (const QByteArray &id) {
                auto object = Sink::ApplicationDomain::ApplicationDomainType::createCopy(id, domainObject);
                return facade->copy(object, newResource).onError([](const KAsync::Error &error) { SinkWarning() << "Failed to copy " << error; });
        });
    }
    return facade->copy(domainObject, newResource).addToContext(std::shared_ptr<void>(facade)).onError([](const KAsync::Error &error) { SinkWarning() << "Failed to copy " << error; });
}

template <class DomainType>
KAsync::Job<void> Store::remove(const DomainType &domainObject)
{
    SinkLog() << "Remove: " << domainObject;
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    if (domainObject.isAggregate()) {
        return KAsync::value(domainObject.aggregatedIds())
            .addToContext(std::shared_ptr<void>(facade))
            .each([=] (const QByteArray &id) {
                auto object = Sink::ApplicationDomain::ApplicationDomainType::createCopy(id, domainObject);
                return facade->remove(object).onError([](const KAsync::Error &error) { SinkWarning() << "Failed to remove " << error; });
        });
    }
    return facade->remove(domainObject).addToContext(std::shared_ptr<void>(facade)).onError([](const KAsync::Error &error) { SinkWarning() << "Failed to remove " << error; });
}

template <class DomainType>
KAsync::Job<void> Store::remove(const Sink::Query &query)
{
    SinkLog() << "Remove: " << query;
    return fetchAll<DomainType>(query)
        .each([] (const typename DomainType::Ptr &entity) {
            return remove(*entity);
        });
}

KAsync::Job<void> Store::removeDataFromDisk(const QByteArray &identifier)
{
    // All databases are going to become invalid, nuke the environments
    // TODO: all clients should react to a notification from the resource
    Sink::Storage::DataStore::clearEnv();
    SinkTrace() << "Remove data from disk " << identifier;
    auto time = QSharedPointer<QTime>::create();
    time->start();
    auto resourceAccess = ResourceAccessFactory::instance().getAccess(identifier, ResourceConfig::getResourceType(identifier));
    resourceAccess->open();
    return resourceAccess->sendCommand(Sink::Commands::RemoveFromDiskCommand)
        .addToContext(resourceAccess)
        .then<void>([resourceAccess](KAsync::Future<void> &future) {
            if (resourceAccess->isReady()) {
                //Wait for the resource shutdown
                auto guard = new QObject;
                QObject::connect(resourceAccess.data(), &ResourceAccess::ready, guard, [&future, guard](bool ready) {
                    if (!ready) {
                        //We don't disconnect if ResourceAccess get's recycled, so ready can fire multiple times, which can result in a crash if the future is no longer valid.
                        delete guard;
                        future.setFinished();
                    }
                });
            } else {
                future.setFinished();
            }
        })
        .then([time]() {
            SinkTrace() << "Remove from disk complete." << Log::TraceTime(time->elapsed());
        });
}

static KAsync::Job<Store::UpgradeResult> upgrade(const QByteArray &resource)
{
    auto store = Sink::Storage::DataStore(Sink::storageLocation(), resource, Sink::Storage::DataStore::ReadOnly);
    if (!store.exists() || Storage::DataStore::databaseVersion(store.createTransaction(Storage::DataStore::ReadOnly)) == Sink::latestDatabaseVersion()) {
        return KAsync::value(Store::UpgradeResult{false});
    }
    SinkLog() << "Upgrading " << resource;

    //We're not using the factory to avoid getting a cached resourceaccess with the wrong resourceType
    auto resourceAccess = Sink::ResourceAccess::Ptr{new Sink::ResourceAccess(resource, ResourceConfig::getResourceType(resource)), &QObject::deleteLater};
    //We first shutdown the resource, because the upgrade runs on start
    return Sink::ResourceControl::shutdown(resource)
        .then(resourceAccess->sendCommand(Sink::Commands::UpgradeCommand))
        .addToContext(resourceAccess)
        .then([=](const KAsync::Error &error) {
            if (error) {
                SinkWarning() << "Error during upgrade.";
                return KAsync::error(error);
            }
            SinkTrace() << "Upgrade of resource " << resource << " complete.";
            return KAsync::null();
        })
        .then(KAsync::value(Store::UpgradeResult{true}));
}

KAsync::Job<Store::UpgradeResult> Store::upgrade()
{
    SinkLog() << "Upgrading...";

    //Migrate from sink.dav to sink.carddav
    const auto resources = ResourceConfig::getResources();
    for (auto it = resources.constBegin(); it != resources.constEnd(); it++) {
        if (it.value() == "sink.dav") {
            ResourceConfig::setResourceType(it.key(), "sink.carddav");
        }
    }

    auto ret = QSharedPointer<bool>::create(false);
    return fetchAll<ApplicationDomain::SinkResource>({})
        .template each([ret](const ApplicationDomain::SinkResource::Ptr &resource) -> KAsync::Job<void> {
            return Sink::upgrade(resource->identifier())
                .then([ret](UpgradeResult returnValue) {
                    if (returnValue.upgradeExecuted) {
                        SinkLog() << "Upgrade executed.";
                        *ret = true;
                    }
                });
        })
        .then([ret] {
            if (*ret) {
                SinkLog() << "Upgrade complete.";
            }
            return Store::UpgradeResult{*ret};
        });
}

static KAsync::Job<void> synchronize(const QByteArray &resource, const Sink::SyncScope &scope)
{
    SinkLog() << "Synchronizing " << resource << scope;
    auto resourceAccess = ResourceAccessFactory::instance().getAccess(resource, ResourceConfig::getResourceType(resource));
    return resourceAccess->synchronizeResource(scope)
        .addToContext(resourceAccess)
        .then([=](const KAsync::Error &error) {
            if (error) {
                SinkWarning() << "Error during sync.";
                return KAsync::error(error);
            }
            SinkTrace() << "Synchronization of resource " << resource << " complete.";
            return KAsync::null();
        });
}

KAsync::Job<void> Store::synchronize(const Sink::Query &query)
{
    return synchronize(Sink::SyncScope{query});
}

KAsync::Job<void> Store::synchronize(const Sink::SyncScope &scope)
{
    SinkLog() << "Synchronizing all resource matching: " << scope;
    return forEachResource(scope, [=] (const auto &resource) {
            return synchronize(resource->identifier(), scope);
        });
}

KAsync::Job<void> Store::abortSynchronization(const Sink::SyncScope &scope)
{
    return forEachResource(scope, [] (const auto &resource) {
        auto resourceAccess = ResourceAccessFactory::instance().getAccess(resource->identifier(), ResourceConfig::getResourceType(resource->identifier()));
        return resourceAccess->sendCommand(Sink::Commands::AbortSynchronizationCommand)
            .addToContext(resourceAccess)
            .then([=](const KAsync::Error &error) {
                if (error) {
                    SinkWarning() << "Error aborting synchronization.";
                    return KAsync::error(error);
                }
                return KAsync::null();
            });
        });
}

template <class DomainType>
KAsync::Job<DomainType> Store::fetchOne(const Sink::Query &query)
{
    return fetch<DomainType>(query, 1).template then<DomainType, QList<typename DomainType::Ptr>>([](const QList<typename DomainType::Ptr> &list) {
        return KAsync::value(*list.first());
    });
}

template <class DomainType>
KAsync::Job<QList<typename DomainType::Ptr>> Store::fetchAll(const Sink::Query &query)
{
    return fetch<DomainType>(query);
}

template <class DomainType>
KAsync::Job<QList<typename DomainType::Ptr>> Store::fetch(const Sink::Query &query, int minimumAmount)
{
    Q_ASSERT(sanityCheckQuery(query));
    auto model = loadModel<DomainType>(query);
    auto list = QSharedPointer<QList<typename DomainType::Ptr>>::create();
    auto context = QSharedPointer<QObject>::create();
    return KAsync::start<QList<typename DomainType::Ptr>>([model, list, context, minimumAmount](KAsync::Future<QList<typename DomainType::Ptr>> &future) {
        if (model->rowCount() >= 1) {
            for (int i = 0; i < model->rowCount(); i++) {
                list->append(model->index(i, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).template value<typename DomainType::Ptr>());
            }
        } else {
            QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, context.data(), [model, list](const QModelIndex &index, int start, int end) {
                for (int i = start; i <= end; i++) {
                    list->append(model->index(i, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).template value<typename DomainType::Ptr>());
                }
            });
            QObject::connect(model.data(), &QAbstractItemModel::dataChanged, context.data(),
                [model, &future, list, minimumAmount](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
                    if (roles.contains(ModelResult<DomainType, typename DomainType::Ptr>::ChildrenFetchedRole)) {
                        if (list->size() < minimumAmount) {
                            future.setError(1, "Not enough values.");
                        } else {
                            future.setValue(*list);
                            future.setFinished();
                        }
                    }
                });
        }
        if (model->data(QModelIndex(), ModelResult<DomainType, typename DomainType::Ptr>::ChildrenFetchedRole).toBool()) {
            if (list->size() < minimumAmount) {
                future.setError(1, "Not enough values.");
            } else {
                future.setValue(*list);
            }
            future.setFinished();
        }
    });
}

template <class DomainType>
DomainType Store::readOne(const Sink::Query &query)
{
    const auto list = read<DomainType>(query);
    if (!list.isEmpty()) {
        return list.first();
    }
    SinkWarning() << "Tried to read value but no values are available.";
    return DomainType();
}

template <class DomainType>
QList<DomainType> Store::read(const Sink::Query &query_)
{
    Q_ASSERT(sanityCheckQuery(query_));
    auto query = query_;
    query.setFlags(Query::SynchronousQuery);

    auto ctx = getQueryContext(query, ApplicationDomain::getTypeName<DomainType>());

    QList<DomainType> list;

    auto result = getEmitter<DomainType>(query, ctx);
    auto aggregatingEmitter = result.first;
    aggregatingEmitter->onAdded([&list, ctx](const typename DomainType::Ptr &value){
        SinkTraceCtx(ctx) << "Found value: " << value->identifier();
        list << *value;
    });

    if (auto resourceEmitter = result.second) {
        resourceEmitter->fetch();
    }

    aggregatingEmitter->fetch();
    return list;
}

#define REGISTER_TYPE(T)                                                          \
    template KAsync::Job<void> Store::remove<T>(const T &domainObject);           \
    template KAsync::Job<void> Store::remove<T>(const Query &);           \
    template KAsync::Job<void> Store::create<T>(const T &domainObject);           \
    template KAsync::Job<void> Store::modify<T>(const T &domainObject);           \
    template KAsync::Job<void> Store::modify<T>(const Query &, const T &);           \
    template KAsync::Job<void> Store::move<T>(const T &domainObject, const QByteArray &newResource);           \
    template KAsync::Job<void> Store::copy<T>(const T &domainObject, const QByteArray &newResource);           \
    template QSharedPointer<QAbstractItemModel> Store::loadModel<T>(const Query &query); \
    template void Store::updateModel<T>(const Query &, const QSharedPointer<QAbstractItemModel> &); \
    template KAsync::Job<T> Store::fetchOne<T>(const Query &);                    \
    template KAsync::Job<QList<T::Ptr>> Store::fetchAll<T>(const Query &);        \
    template KAsync::Job<QList<T::Ptr>> Store::fetch<T>(const Query &, int);      \
    template T Store::readOne<T>(const Query &);                                  \
    template QList<T> Store::read<T>(const Query &);

SINK_REGISTER_TYPES()

} // namespace Sink
