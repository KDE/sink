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
#include "facadefactory.h"
#include "modelresult.h"
#include "storage.h"
#include "log.h"

#undef DEBUG_AREA
#define DEBUG_AREA "client.store"

namespace Sink {

QString Store::storageLocation()
{
    return Sink::storageLocation();
}

static QList<QByteArray> getResources(const QList<QByteArray> &resourceFilter, const QByteArray &type = QByteArray())
{
    // Return the global resource (signified by an empty name) for types that don't belong to a specific resource
    if (type == "sinkresource" || type == "sinkaccount") {
        return QList<QByteArray>() << "";
    }
    QList<QByteArray> resources;
    const auto configuredResources = ResourceConfig::getResources();
    if (resourceFilter.isEmpty()) {
        for (const auto &res : configuredResources.keys()) {
            // TODO filter by entity type
            resources << res;
        }
    } else {
        for (const auto &res : resourceFilter) {
            if (configuredResources.contains(res)) {
                resources << res;
            } else {
                qWarning() << "Resource is not existing: " << res;
            }
        }
    }
    Trace() << "Found resources: " << resources;
    return resources;
}

template <class DomainType>
QSharedPointer<QAbstractItemModel> Store::loadModel(Query query)
{
    Trace() << "Query: " << ApplicationDomain::getTypeName<DomainType>();
    Trace() << "  Requested: " << query.requestedProperties;
    Trace() << "  Filter: " << query.propertyFilter;
    Trace() << "  Parent: " << query.parentProperty;
    Trace() << "  Ids: " << query.ids;
    Trace() << "  IsLive: " << query.liveQuery;
    Trace() << "  Sorting: " << query.sortProperty;
    auto model = QSharedPointer<ModelResult<DomainType, typename DomainType::Ptr>>::create(query, query.requestedProperties);

    //* Client defines lifetime of model
    //* The model lifetime defines the duration of live-queries
    //* The facade needs to life for the duration of any calls being made (assuming we get rid of any internal callbacks
    //* The emitter needs to live or the duration of query (respectively, the model)
    //* The result provider needs to live for as long as results are provided (until the last thread exits).

    // Query all resources and aggregate results
    auto resources = getResources(query.resources, ApplicationDomain::getTypeName<DomainType>());
    auto aggregatingEmitter = AggregatingResultEmitter<typename DomainType::Ptr>::Ptr::create();
    model->setEmitter(aggregatingEmitter);
    KAsync::iterate(resources)
        .template each<void, QByteArray>([query, aggregatingEmitter](const QByteArray &resource, KAsync::Future<void> &future) {
            auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceName(resource), resource);
            if (facade) {
                Trace() << "Trying to fetch from resource " << resource;
                auto result = facade->load(query);
                aggregatingEmitter->addEmitter(result.second);
                result.first.template then<void>([&future]() { future.setFinished(); }).exec();
            } else {
                Trace() << "Couldn' find a facade for " << resource;
                // Ignore the error and carry on
                future.setFinished();
            }
        })
        .exec();
    model->fetchMore(QModelIndex());

    return model;
}

template <class DomainType>
static std::shared_ptr<StoreFacade<DomainType>> getFacade(const QByteArray &resourceInstanceIdentifier)
{
    if (auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceName(resourceInstanceIdentifier), resourceInstanceIdentifier)) {
        return facade;
    }
    return std::make_shared<NullFacade<DomainType>>();
}

template <class DomainType>
KAsync::Job<void> Store::create(const DomainType &domainObject)
{
    // Potentially move to separate thread as well
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    return facade->create(domainObject).template then<void>([facade]() {}, [](int errorCode, const QString &error) { Warning() << "Failed to create"; });
}

template <class DomainType>
KAsync::Job<void> Store::modify(const DomainType &domainObject)
{
    // Potentially move to separate thread as well
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    return facade->modify(domainObject).template then<void>([facade]() {}, [](int errorCode, const QString &error) { Warning() << "Failed to modify"; });
}

template <class DomainType>
KAsync::Job<void> Store::remove(const DomainType &domainObject)
{
    // Potentially move to separate thread as well
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    return facade->remove(domainObject).template then<void>([facade]() {}, [](int errorCode, const QString &error) { Warning() << "Failed to remove"; });
}

KAsync::Job<void> Store::removeDataFromDisk(const QByteArray &identifier)
{
    // All databases are going to become invalid, nuke the environments
    // TODO: all clients should react to a notification the resource
    Sink::Storage::clearEnv();
    Trace() << "Remove data from disk " << identifier;
    auto time = QSharedPointer<QTime>::create();
    time->start();
    auto resourceAccess = ResourceAccessFactory::instance().getAccess(identifier);
    resourceAccess->open();
    return resourceAccess->sendCommand(Sink::Commands::RemoveFromDiskCommand)
        .then<void>([resourceAccess, time]() { Trace() << "Remove from disk complete." << Log::TraceTime(time->elapsed()); });
}

KAsync::Job<void> Store::synchronize(const Sink::Query &query)
{
    Trace() << "synchronize" << query.resources;
    auto resources = getResources(query.resources);
    return KAsync::iterate(resources)
        .template each<void, QByteArray>([query](const QByteArray &resource, KAsync::Future<void> &future) {
            Trace() << "Synchronizing " << resource;
            auto resourceAccess = ResourceAccessFactory::instance().getAccess(resource);
            resourceAccess->open();
            resourceAccess->synchronizeResource(true, false).then<void>([&future, resourceAccess]() { future.setFinished(); }).exec();
        });
}

template <class DomainType>
KAsync::Job<DomainType> Store::fetchOne(const Sink::Query &query)
{
    return KAsync::start<DomainType>([query](KAsync::Future<DomainType> &future) {
        // FIXME We could do this more elegantly if composed jobs would have the correct type (In that case we'd simply return the value from then continuation, and could avoid the
        // outer job entirely)
        fetch<DomainType>(query, 1)
            .template then<void, QList<typename DomainType::Ptr>>(
                [&future](const QList<typename DomainType::Ptr> &list) {
                    future.setValue(*list.first());
                    future.setFinished();
                },
                [&future](int errorCode, const QString &errorMessage) {
                    future.setError(errorCode, errorMessage);
                    future.setFinished();
                })
            .exec();
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
                        }
                        future.setFinished();
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

#define REGISTER_TYPE(T)                                                          \
    template KAsync::Job<void> Store::remove<T>(const T &domainObject);           \
    template KAsync::Job<void> Store::create<T>(const T &domainObject);           \
    template KAsync::Job<void> Store::modify<T>(const T &domainObject);           \
    template QSharedPointer<QAbstractItemModel> Store::loadModel<T>(Query query); \
    template KAsync::Job<T> Store::fetchOne<T>(const Query &);                    \
    template KAsync::Job<QList<T::Ptr>> Store::fetchAll<T>(const Query &);        \
    template KAsync::Job<QList<T::Ptr>> Store::fetch<T>(const Query &, int);

REGISTER_TYPE(ApplicationDomain::Event);
REGISTER_TYPE(ApplicationDomain::Mail);
REGISTER_TYPE(ApplicationDomain::Folder);
REGISTER_TYPE(ApplicationDomain::SinkResource);
REGISTER_TYPE(ApplicationDomain::SinkAccount);

} // namespace Sink
