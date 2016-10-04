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
#include <QUuid>
#include <functional>
#include <memory>

#include "resourceaccess.h"
#include "commands.h"
#include "resourcefacade.h"
#include "definitions.h"
#include "resourceconfig.h"
#include "facadefactory.h"
#include "modelresult.h"
#include "storage.h"
#include "log.h"

SINK_DEBUG_AREA("store")

Q_DECLARE_METATYPE(QSharedPointer<Sink::ResultEmitter<Sink::ApplicationDomain::SinkResource::Ptr>>)
Q_DECLARE_METATYPE(QSharedPointer<Sink::ResourceAccessInterface>);
Q_DECLARE_METATYPE(std::shared_ptr<void>);

namespace Sink {

QString Store::storageLocation()
{
    return Sink::storageLocation();
}

QString Store::getTemporaryFilePath()
{
    return Sink::temporaryFileLocation() + "/" + QUuid::createUuid().toString();
}

/*
 * Returns a map of resource instance identifiers and resource type
 */
static QMap<QByteArray, QByteArray> getResources(const QList<QByteArray> &resourceFilter, const QList<QByteArray> &accountFilter,const QByteArray &type = QByteArray())
{
    const auto filterResource = [&](const QByteArray &res) {
        const auto configuration = ResourceConfig::getConfiguration(res);
        if (!accountFilter.isEmpty() && !accountFilter.contains(configuration.value("account").toByteArray())) {
            return true;
        }
        return false;
    };

    QMap<QByteArray, QByteArray> resources;
    // Return the global resource (signified by an empty name) for types that don't belong to a specific resource
    if (ApplicationDomain::isGlobalType(type)) {
        resources.insert("", "");
        return resources;
    }
    const auto configuredResources = ResourceConfig::getResources();
    if (resourceFilter.isEmpty()) {
        for (const auto &res : configuredResources.keys()) {
            const auto type = configuredResources.value(res);
            if (filterResource(res)) {
                continue;
            }
            // TODO filter by entity type
            resources.insert(res, type);
        }
    } else {
        for (const auto &res : resourceFilter) {
            if (configuredResources.contains(res)) {
                if (filterResource(res)) {
                    continue;
                }
                resources.insert(res, configuredResources.value(res));
            } else {
                SinkWarning() << "Resource is not existing: " << res;
            }
        }
    }
    SinkTrace() << "Found resources: " << resources;
    return resources;
}


template <class DomainType>
KAsync::Job<void> queryResource(const QByteArray resourceType, const QByteArray &resourceInstanceIdentifier, const Query &query, typename AggregatingResultEmitter<typename DomainType::Ptr>::Ptr aggregatingEmitter)
{
    auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceType, resourceInstanceIdentifier);
    if (facade) {
        SinkTrace() << "Trying to fetch from resource " << resourceInstanceIdentifier;
        auto result = facade->load(query);
        if (result.second) {
            aggregatingEmitter->addEmitter(result.second);
        } else {
            SinkWarning() << "Null emitter for resource " << resourceInstanceIdentifier;
        }
        return result.first;
    } else {
        SinkTrace() << "Couldn' find a facade for " << resourceInstanceIdentifier;
        // Ignore the error and carry on
        return KAsync::null<void>();
    }
}

template <class DomainType>
QSharedPointer<QAbstractItemModel> Store::loadModel(Query query)
{
    SinkTrace() << "Query: " << ApplicationDomain::getTypeName<DomainType>();
    SinkTrace() << "  Requested: " << query.requestedProperties;
    SinkTrace() << "  Filter: " << query.propertyFilter;
    SinkTrace() << "  Parent: " << query.parentProperty;
    SinkTrace() << "  Ids: " << query.ids;
    SinkTrace() << "  IsLive: " << query.liveQuery;
    SinkTrace() << "  Sorting: " << query.sortProperty;
    auto model = QSharedPointer<ModelResult<DomainType, typename DomainType::Ptr>>::create(query, query.requestedProperties);

    //* Client defines lifetime of model
    //* The model lifetime defines the duration of live-queries
    //* The facade needs to life for the duration of any calls being made (assuming we get rid of any internal callbacks
    //* The emitter needs to live or the duration of query (respectively, the model)
    //* The result provider needs to live for as long as results are provided (until the last thread exits).

    // Query all resources and aggregate results
    auto resources = getResources(query.resources, query.accounts, ApplicationDomain::getTypeName<DomainType>());
    auto aggregatingEmitter = AggregatingResultEmitter<typename DomainType::Ptr>::Ptr::create();
    model->setEmitter(aggregatingEmitter);

    if (query.liveQuery && query.resources.isEmpty() && !ApplicationDomain::isGlobalType(ApplicationDomain::getTypeName<DomainType>())) {
        SinkTrace() << "Listening for new resources";
        auto facade = FacadeFactory::instance().getFacade<ApplicationDomain::SinkResource>("", "");
        Q_ASSERT(facade);
        Sink::Query resourceQuery;
        resourceQuery.liveQuery = query.liveQuery;
        auto result = facade->load(resourceQuery);
        auto emitter = result.second;
        emitter->onAdded([query, aggregatingEmitter](const ApplicationDomain::SinkResource::Ptr &resource) {
            SinkTrace() << "Found new resources: " << resource->identifier();
            const auto resourceType = ResourceConfig::getResourceType(resource->identifier());
            Q_ASSERT(!resourceType.isEmpty());
            queryResource<DomainType>(resourceType, resource->identifier(), query, aggregatingEmitter).exec();
        });
        emitter->onModified([](const ApplicationDomain::SinkResource::Ptr &) {
        });
        emitter->onRemoved([](const ApplicationDomain::SinkResource::Ptr &) {
        });
        emitter->onInitialResultSetComplete([](const ApplicationDomain::SinkResource::Ptr &) {
        });
        emitter->onComplete([query, aggregatingEmitter]() {
            SinkTrace() << "Resource query complete";

        });
        model->setProperty("resourceEmitter", QVariant::fromValue(emitter));
        result.first.exec();
    }

    KAsync::value(resources.keys())
        .template each([query, aggregatingEmitter, resources](const QByteArray &resourceInstanceIdentifier) {
            const auto resourceType = resources.value(resourceInstanceIdentifier);
            return queryResource<DomainType>(resourceType, resourceInstanceIdentifier, query, aggregatingEmitter);
        })
        .exec();
    model->fetchMore(QModelIndex());

    return model;
}

template <class DomainType>
static std::shared_ptr<StoreFacade<DomainType>> getFacade(const QByteArray &resourceInstanceIdentifier)
{
    if (ApplicationDomain::isGlobalType(ApplicationDomain::getTypeName<DomainType>())) {
        if (auto facade = FacadeFactory::instance().getFacade<DomainType>("", "")) {
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
    // Potentially move to separate thread as well
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    return facade->create(domainObject).addToContext(std::shared_ptr<void>(facade)).onError([](const KAsync::Error &error) { SinkWarning() << "Failed to create"; });
}

template <class DomainType>
KAsync::Job<void> Store::modify(const DomainType &domainObject)
{
    // Potentially move to separate thread as well
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    return facade->modify(domainObject).addToContext(std::shared_ptr<void>(facade)).onError([](const KAsync::Error &error) { SinkWarning() << "Failed to modify"; });
}

template <class DomainType>
KAsync::Job<void> Store::remove(const DomainType &domainObject)
{
    // Potentially move to separate thread as well
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    return facade->remove(domainObject).addToContext(std::shared_ptr<void>(facade)).onError([](const KAsync::Error &error) { SinkWarning() << "Failed to remove"; });
}

KAsync::Job<void> Store::removeDataFromDisk(const QByteArray &identifier)
{
    // All databases are going to become invalid, nuke the environments
    // TODO: all clients should react to a notification the resource
    Sink::Storage::clearEnv();
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
                QObject::connect(resourceAccess.data(), &ResourceAccess::ready, [&future](bool ready) {
                    if (!ready) {
                        future.setFinished();
                    }
                });
            } else {
                future.setFinished();
            }
        })
        .syncThen<void>([time]() {
            SinkTrace() << "Remove from disk complete." << Log::TraceTime(time->elapsed());
        });
}

KAsync::Job<void> Store::synchronize(const Sink::Query &query)
{
    SinkTrace() << "synchronize" << query.resources;
    auto resources = getResources(query.resources, query.accounts).keys();
    //FIXME only necessary because each doesn't propagate errors
    auto errorFlag = new bool;
    return KAsync::value(resources)
        .template each([query, errorFlag](const QByteArray &resource) {
            SinkTrace() << "Synchronizing " << resource;
            auto resourceAccess = ResourceAccessFactory::instance().getAccess(resource, ResourceConfig::getResourceType(resource));
            resourceAccess->open();
            return resourceAccess->synchronizeResource(true, false)
                .addToContext(resourceAccess)
                .then<void>([errorFlag](const KAsync::Error &error) {
                        if (error) {
                            *errorFlag = true;
                            SinkWarning() << "Error during sync.";
                            return KAsync::error<void>(error);
                        }
                        SinkTrace() << "synced.";
                        return KAsync::null<void>();
                    });
        })
        .then<void>([errorFlag]() {
            if (*errorFlag) {
                return KAsync::error<void>("Error during sync.");
            }
            delete errorFlag;
            return KAsync::null<void>();
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
    auto model = loadModel<DomainType>(query);
    auto list = QSharedPointer<QList<typename DomainType::Ptr>>::create();
    auto context = QSharedPointer<QObject>::create();
    return KAsync::start<QList<typename DomainType::Ptr>>([model, list, context, minimumAmount](KAsync::Future<QList<typename DomainType::Ptr>> &future) {
        if (model->rowCount() >= 1) {
            for (int i = 0; i < model->rowCount(); i++) {
                list->append(model->index(i, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).template value<typename DomainType::Ptr>());
            }
        } else {
            QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, context.data(), [model, &future, list](const QModelIndex &index, int start, int end) {
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
    return DomainType();
}

template <class DomainType>
QList<DomainType> Store::read(const Sink::Query &q)
{
    auto query = q;
    query.synchronousQuery = true;
    query.liveQuery = false;
    QList<DomainType> list;
    auto resources = getResources(query.resources, query.accounts, ApplicationDomain::getTypeName<DomainType>());
    auto aggregatingEmitter = AggregatingResultEmitter<typename DomainType::Ptr>::Ptr::create();
    aggregatingEmitter->onAdded([&list](const typename DomainType::Ptr &value){
        SinkTrace() << "Found value: " << value->identifier();
        list << *value;
    });
    for (const auto &resourceInstanceIdentifier : resources.keys()) {
        const auto resourceType = resources.value(resourceInstanceIdentifier);
        SinkTrace() << "Querying resource:  " << resourceType << resourceInstanceIdentifier;
        auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceType, resourceInstanceIdentifier);
        if (facade) {
            SinkTrace() << "Trying to fetch from resource " << resourceInstanceIdentifier;
            auto result = facade->load(query);
            if (result.second) {
                aggregatingEmitter->addEmitter(result.second);
            } else {
                SinkWarning() << "Null emitter for resource " << resourceInstanceIdentifier;
            }
            result.first.exec();
        } else {
            SinkTrace() << "Couldn't find a facade for " << resourceInstanceIdentifier;
            // Ignore the error and carry on
        }
    }
    aggregatingEmitter->fetch(typename DomainType::Ptr());
    return list;
}

#define REGISTER_TYPE(T)                                                          \
    template KAsync::Job<void> Store::remove<T>(const T &domainObject);           \
    template KAsync::Job<void> Store::create<T>(const T &domainObject);           \
    template KAsync::Job<void> Store::modify<T>(const T &domainObject);           \
    template QSharedPointer<QAbstractItemModel> Store::loadModel<T>(Query query); \
    template KAsync::Job<T> Store::fetchOne<T>(const Query &);                    \
    template KAsync::Job<QList<T::Ptr>> Store::fetchAll<T>(const Query &);        \
    template KAsync::Job<QList<T::Ptr>> Store::fetch<T>(const Query &, int);      \
    template T Store::readOne<T>(const Query &);                                  \
    template QList<T> Store::read<T>(const Query &);

REGISTER_TYPE(ApplicationDomain::Event);
REGISTER_TYPE(ApplicationDomain::Mail);
REGISTER_TYPE(ApplicationDomain::Folder);
REGISTER_TYPE(ApplicationDomain::SinkResource);
REGISTER_TYPE(ApplicationDomain::SinkAccount);
REGISTER_TYPE(ApplicationDomain::Identity);

} // namespace Sink
