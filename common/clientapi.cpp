/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "clientapi.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>
#include <QTime>
#include <QEventLoop>
#include <QAbstractItemModel>
#include <QDir>
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

#undef DEBUG_AREA
#define DEBUG_AREA "client.clientapi"

namespace Sink
{

QString Store::storageLocation()
{
    return Sink::storageLocation();
}

QByteArray Store::resourceName(const QByteArray &instanceIdentifier)
{
    return Sink::resourceName(instanceIdentifier);
}

static QList<QByteArray> getResources(const QList<QByteArray> &resourceFilter, const QByteArray &type)
{
    //Return the global resource (signified by an empty name) for types that don't eblong to a specific resource
    if (type == "sinkresource") {
        return QList<QByteArray>() << "";
    }
    QList<QByteArray> resources;
    const auto configuredResources = ResourceConfig::getResources();
    if (resourceFilter.isEmpty()) {
        for (const auto &res : configuredResources.keys()) {
            //TODO filter by entity type
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
    Trace() << "Query: ";
    Trace() << "  Requested: " << query.requestedProperties;
    Trace() << "  Filter: " << query.propertyFilter;
    Trace() << "  Parent: " << query.parentProperty;
    Trace() << "  Ids: " << query.ids;
    Trace() << "  IsLive: " << query.liveQuery;
    auto model = QSharedPointer<ModelResult<DomainType, typename DomainType::Ptr> >::create(query, query.requestedProperties);

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
            result.first.template then<void>([&future](){future.setFinished();}).exec();
        } else {
            Trace() << "Couldn' find a facade for " << resource;
            //Ignore the error and carry on
            future.setFinished();
        }
    }).exec();
    model->fetchMore(QModelIndex());

    return model;
}

template <class DomainType>
static std::shared_ptr<StoreFacade<DomainType> > getFacade(const QByteArray &resourceInstanceIdentifier)
{
    if (auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceName(resourceInstanceIdentifier), resourceInstanceIdentifier)) {
        return facade;
    }
    return std::make_shared<NullFacade<DomainType> >();
}

template <class DomainType>
KAsync::Job<void> Store::create(const DomainType &domainObject) {
    //Potentially move to separate thread as well
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    return facade->create(domainObject).template then<void>([facade](){}, [](int errorCode, const QString &error) {
        Warning() << "Failed to create";
    });
}

template <class DomainType>
KAsync::Job<void> Store::modify(const DomainType &domainObject)
{
    //Potentially move to separate thread as well
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    return facade->modify(domainObject).template then<void>([facade](){}, [](int errorCode, const QString &error) {
        Warning() << "Failed to modify";
    });
}

template <class DomainType>
KAsync::Job<void> Store::remove(const DomainType &domainObject)
{
    //Potentially move to separate thread as well
    auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
    return facade->remove(domainObject).template then<void>([facade](){}, [](int errorCode, const QString &error) {
        Warning() << "Failed to remove";
    });
}

KAsync::Job<void> Store::shutdown(const QByteArray &identifier)
{
    Trace() << "shutdown " << identifier;
    auto time = QSharedPointer<QTime>::create();
    time->start();
    return ResourceAccess::connectToServer(identifier).then<void, QSharedPointer<QLocalSocket>>([identifier, time](QSharedPointer<QLocalSocket> socket, KAsync::Future<void> &future) {
        //We can't currently reuse the socket
        socket->close();
        auto resourceAccess = QSharedPointer<Sink::ResourceAccess>::create(identifier);
        resourceAccess->open();
        resourceAccess->sendCommand(Sink::Commands::ShutdownCommand).then<void>([&future, resourceAccess, time]() {
            Trace() << "Shutdown complete." << Log::TraceTime(time->elapsed());
            future.setFinished();
        }).exec();
    },
    [](int, const QString &) {
        Trace() << "Resource is already closed.";
        //Resource isn't started, nothing to shutdown
    })
    //FIXME JOBAPI this is only required because we don't care about the return value of connectToServer
    .template then<void>([](){});
}

KAsync::Job<void> Store::start(const QByteArray &identifier)
{
    Trace() << "start " << identifier;
    auto time = QSharedPointer<QTime>::create();
    time->start();
    auto resourceAccess = QSharedPointer<Sink::ResourceAccess>::create(identifier);
    resourceAccess->open();
    return resourceAccess->sendCommand(Sink::Commands::PingCommand).then<void>([resourceAccess, time]() {
        Trace() << "Start complete." << Log::TraceTime(time->elapsed());
    });
}

void Store::removeFromDisk(const QByteArray &identifier)
{
    //TODO By calling the resource executable with a --remove option instead
    //we can ensure that no other resource process is running at the same time
    QDir dir(Sink::storageLocation());
    for (const auto &folder : dir.entryList(QStringList() << identifier + "*")) {
        Sink::Storage(Sink::storageLocation(), folder, Sink::Storage::ReadWrite).removeFromDisk();
    }
}

KAsync::Job<void> Store::synchronize(const Sink::Query &query)
{
    Trace() << "synchronize" << query.resources;
    return KAsync::iterate(query.resources)
    .template each<void, QByteArray>([query](const QByteArray &resource, KAsync::Future<void> &future) {
        Trace() << "Synchronizing " << resource;
        auto resourceAccess = QSharedPointer<Sink::ResourceAccess>::create(resource);
        resourceAccess->open();
        resourceAccess->synchronizeResource(true, false).then<void>([&future, resourceAccess]() {
            future.setFinished();
        }).exec();
    })
    //FIXME JOBAPI this is only required because we don't care about the return value of each (and each shouldn't even have a return value)
    .template then<void>([](){});
}

KAsync::Job<void> Store::flushMessageQueue(const QByteArrayList &resourceIdentifier)
{
    Trace() << "flushMessageQueue" << resourceIdentifier;
    return KAsync::iterate(resourceIdentifier)
    .template each<void, QByteArray>([](const QByteArray &resource, KAsync::Future<void> &future) {
        Trace() << "Flushing message queue " << resource;
        auto resourceAccess = QSharedPointer<Sink::ResourceAccess>::create(resource);
        resourceAccess->open();
        resourceAccess->synchronizeResource(false, true).then<void>([&future, resourceAccess]() {
            future.setFinished();
        }).exec();
    })
    //FIXME JOBAPI this is only required because we don't care about the return value of each (and each shouldn't even have a return value)
    .template then<void>([](){});
}

KAsync::Job<void> Store::flushReplayQueue(const QByteArrayList &resourceIdentifier)
{
    return flushMessageQueue(resourceIdentifier);
}

template <class DomainType>
KAsync::Job<DomainType> Store::fetchOne(const Sink::Query &query)
{
    return KAsync::start<DomainType>([query](KAsync::Future<DomainType> &future) {
        //FIXME We could do this more elegantly if composed jobs would have the correct type (In that case we'd simply return the value from then continuation, and could avoid the outer job entirely)
        fetch<DomainType>(query, 1)
            .template then<void, QList<typename DomainType::Ptr> >([&future](const QList<typename DomainType::Ptr> &list){
                future.setValue(*list.first());
                future.setFinished();
            }, [&future](int errorCode, const QString &errorMessage) {
                future.setError(errorCode, errorMessage);
                future.setFinished();
            }).exec();
    });
}

template <class DomainType>
KAsync::Job<QList<typename DomainType::Ptr> > Store::fetchAll(const Sink::Query &query)
{
    return fetch<DomainType>(query);
}

template <class DomainType>
KAsync::Job<QList<typename DomainType::Ptr> > Store::fetch(const Sink::Query &query, int minimumAmount)
{
    auto model = loadModel<DomainType>(query);
    auto list = QSharedPointer<QList<typename DomainType::Ptr> >::create();
    auto context = QSharedPointer<QObject>::create();
    return KAsync::start<QList<typename DomainType::Ptr> >([model, list, context, minimumAmount](KAsync::Future<QList<typename DomainType::Ptr> > &future) {
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
            QObject::connect(model.data(), &QAbstractItemModel::dataChanged, context.data(), [model, &future, list, minimumAmount](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
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

template <class DomainType>
KAsync::Job<void> Resources::inspect(const Inspection &inspectionCommand)
{
    auto resource = inspectionCommand.resourceIdentifier;

    auto time = QSharedPointer<QTime>::create();
    time->start();
    Trace() << "Sending inspection " << resource;
    auto resourceAccess = QSharedPointer<Sink::ResourceAccess>::create(resource);
    resourceAccess->open();
    auto notifier = QSharedPointer<Sink::Notifier>::create(resourceAccess);
    auto id = QUuid::createUuid().toByteArray();
    return resourceAccess->sendInspectionCommand(id, ApplicationDomain::getTypeName<DomainType>(), inspectionCommand.entityIdentifier, inspectionCommand.property, inspectionCommand.expectedValue)
        .template then<void>([resourceAccess, notifier, id, time](KAsync::Future<void> &future) {
            notifier->registerHandler([&future, id, time](const Notification &notification) {
                if (notification.id == id) {
                    Trace() << "Inspection complete." << Log::TraceTime(time->elapsed());
                    if (notification.code) {
                        future.setError(-1, "Inspection returned an error: " + notification.message);
                    } else {
                        future.setFinished();
                    }
                }
            });
        });
}

class Sink::Notifier::Private {
public:
    Private()
        : context(new QObject)
    {

    }
    QList<QSharedPointer<ResourceAccess> > resourceAccess;
    QList<std::function<void(const Notification &)> > handler;
    QSharedPointer<QObject> context;
};

Notifier::Notifier(const QSharedPointer<ResourceAccess> &resourceAccess)
    : d(new Sink::Notifier::Private)
{
    QObject::connect(resourceAccess.data(), &ResourceAccess::notification, d->context.data(), [this](const Notification &notification) {
        for (const auto &handler : d->handler) {
            handler(notification);
        }
    });
    d->resourceAccess << resourceAccess;
}

Notifier::Notifier(const QByteArray &instanceIdentifier)
    : d(new Sink::Notifier::Private)
{
    auto resourceAccess = Sink::ResourceAccess::Ptr::create(instanceIdentifier);
    resourceAccess->open();
    QObject::connect(resourceAccess.data(), &ResourceAccess::notification, d->context.data(), [this](const Notification &notification) {
        for (const auto &handler : d->handler) {
            handler(notification);
        }
    });
    d->resourceAccess << resourceAccess;
}

void Notifier::registerHandler(std::function<void(const Notification &)> handler)
{
    d->handler << handler;
}

#define REGISTER_TYPE(T) template KAsync::Job<void> Store::remove<T>(const T &domainObject); \
    template KAsync::Job<void> Store::create<T>(const T &domainObject); \
    template KAsync::Job<void> Store::modify<T>(const T &domainObject); \
    template QSharedPointer<QAbstractItemModel> Store::loadModel<T>(Query query); \
    template KAsync::Job<void> Resources::inspect<T>(const Inspection &); \
    template KAsync::Job<T> Store::fetchOne<T>(const Query &); \
    template KAsync::Job<QList<T::Ptr> > Store::fetchAll<T>(const Query &); \
    template KAsync::Job<QList<T::Ptr> > Store::fetch<T>(const Query &, int); \

REGISTER_TYPE(ApplicationDomain::Event);
REGISTER_TYPE(ApplicationDomain::Mail);
REGISTER_TYPE(ApplicationDomain::Folder);
REGISTER_TYPE(ApplicationDomain::SinkResource);

} // namespace Sink

