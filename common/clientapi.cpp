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
#include <QEventLoop>
#include <QAbstractItemModel>
#include <QDir>
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

namespace Akonadi2
{

QString Store::storageLocation()
{
    return Akonadi2::storageLocation();
}

QByteArray Store::resourceName(const QByteArray &instanceIdentifier)
{
    return Akonadi2::resourceName(instanceIdentifier);
}

QList<QByteArray> Store::getResources(const QList<QByteArray> &resourceFilter, const QByteArray &type)
{
    //Return the global resource (signified by an empty name) for types that don't eblong to a specific resource
    if (type == "akonadiresource") {
        qWarning() << "Global resource";
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
    qWarning() << "Found resources: " << resources;
    return resources;
}

template <class DomainType>
QSharedPointer<QAbstractItemModel> Store::loadModel(Query query)
{
    auto model = QSharedPointer<ModelResult<DomainType, typename DomainType::Ptr> >::create(query, query.requestedProperties);

    //* Client defines lifetime of model
    //* The model lifetime defines the duration of live-queries
    //* The facade needs to life for the duration of any calls being made (assuming we get rid of any internal callbacks
    //* The emitter needs to live or the duration of query (respectively, the model)
    //* The result provider needs to live for as long as results are provided (until the last thread exits).

    // Query all resources and aggregate results
    auto resources = getResources(query.resources, ApplicationDomain::getTypeName<DomainType>());
    if (resources.isEmpty()) {
        Warning() << "No resources available.";
        auto resultProvider = Akonadi2::ResultProvider<typename DomainType::Ptr>::Ptr::create();
        model->setEmitter(resultProvider->emitter());
        resultProvider->initialResultSetComplete(typename DomainType::Ptr());
        return model;
    }
    KAsync::iterate(resources)
    .template each<void, QByteArray>([query, model](const QByteArray &resource, KAsync::Future<void> &future) {
        auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceName(resource), resource);
        if (facade) {
            Trace() << "Trying to fetch from resource";
            auto result = facade->load(query);
            auto emitter = result.second;
            //TODO use aggregating emitter instead
            model->setEmitter(emitter);
            model->fetchMore(QModelIndex());
            result.first.template then<void>([&future](){future.setFinished();}).exec();
        } else {
            //Ignore the error and carry on
            future.setFinished();
        }
    }).exec();

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
    return ResourceAccess::connectToServer(identifier).then<void, QSharedPointer<QLocalSocket>>([identifier](QSharedPointer<QLocalSocket> socket, KAsync::Future<void> &future) {
        //We can't currently reuse the socket
        socket->close();
        auto resourceAccess = QSharedPointer<Akonadi2::ResourceAccess>::create(identifier);
        resourceAccess->open();
        resourceAccess->sendCommand(Akonadi2::Commands::ShutdownCommand).then<void>([&future, resourceAccess]() {
            Trace() << "Shutdown complete";
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
    auto resourceAccess = QSharedPointer<Akonadi2::ResourceAccess>::create(identifier);
    resourceAccess->open();
    return resourceAccess->sendCommand(Akonadi2::Commands::PingCommand).then<void>([resourceAccess]() {
        Trace() << "Start complete";
    });
}

void Store::removeFromDisk(const QByteArray &identifier)
{
    //TODO By calling the resource executable with a --remove option instead
    //we can ensure that no other resource process is running at the same time
    QDir dir(Akonadi2::storageLocation());
    for (const auto &folder : dir.entryList(QStringList() << identifier + "*")) {
        Akonadi2::Storage(Akonadi2::storageLocation(), folder, Akonadi2::Storage::ReadWrite).removeFromDisk();
    }
}

KAsync::Job<void> Store::synchronize(const Akonadi2::Query &query)
{
    Trace() << "synchronize";
    return  KAsync::iterate(query.resources)
    .template each<void, QByteArray>([query](const QByteArray &resource, KAsync::Future<void> &future) {
        auto resourceAccess = QSharedPointer<Akonadi2::ResourceAccess>::create(resource);
        resourceAccess->open();
        resourceAccess->synchronizeResource(query.syncOnDemand, query.processAll).then<void>([&future, resourceAccess]() {
            future.setFinished();
        }).exec();
    })
    //FIXME JOBAPI this is only required because we don't care about the return value of each (and each shouldn't even have a return value)
    .template then<void>([](){});
}

#define REGISTER_TYPE(T) template KAsync::Job<void> Store::remove<T>(const T &domainObject); \
    template KAsync::Job<void> Store::create<T>(const T &domainObject); \
    template KAsync::Job<void> Store::modify<T>(const T &domainObject); \
    template QSharedPointer<QAbstractItemModel> Store::loadModel<T>(Query query); \

REGISTER_TYPE(ApplicationDomain::Event);
REGISTER_TYPE(ApplicationDomain::Mail);
REGISTER_TYPE(ApplicationDomain::Folder);
REGISTER_TYPE(ApplicationDomain::AkonadiResource);

} // namespace Akonadi2

