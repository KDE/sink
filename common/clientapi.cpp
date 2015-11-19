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
#include <functional>
#include <memory>

#include "resourceaccess.h"
#include "commands.h"
#include "resourcefacade.h"
#include "log.h"
#include "definitions.h"
#include "resourceconfig.h"
#include "facadefactory.h"
#include "log.h"

#define ASYNCINTHREAD

namespace async
{
    void run(const std::function<void()> &runner) {
        auto timer = new QTimer();
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, [runner, timer]() {
            delete timer;
#ifndef ASYNCINTHREAD
            runner();
#else
            QtConcurrent::run(runner);
#endif
        });
        timer->start(0);
    };
} // namespace async


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
QSharedPointer<ResultEmitter<typename DomainType::Ptr> > Store::load(Query query)
{
    auto resultSet = QSharedPointer<ResultProvider<typename DomainType::Ptr> >::create();

    //Execute the search in a thread.
    //We must guarantee that the emitter is returned before the first result is emitted.
    //The result provider must be threadsafe.
    async::run([query, resultSet](){
        QEventLoop eventLoop;
        resultSet->onDone([&eventLoop](){
            eventLoop.quit();
        });
        // Query all resources and aggregate results
        KAsync::iterate(getResources(query.resources, ApplicationDomain::getTypeName<DomainType>()))
        .template each<void, QByteArray>([query, resultSet](const QByteArray &resource, KAsync::Future<void> &future) {
            if (auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceName(resource), resource)) {
                facade->load(query, *resultSet).template then<void>([&future](){future.setFinished();}).exec();
                //Keep the facade alive for the lifetime of the resultSet.
                resultSet->setFacade(facade);
            } else {
                //Ignore the error and carry on
                future.setFinished();
            }
        }).template then<void>([query, resultSet]() {
            resultSet->initialResultSetComplete();
            if (!query.liveQuery) {
                resultSet->complete();
            }
        }).exec();

        //Keep the thread alive until the result is ready
        if (!resultSet->isDone()) {
            eventLoop.exec();
        }
    });
    return resultSet->emitter();
}

template <class DomainType>
QSharedPointer<QAbstractItemModel> Store::loadModel(Query query)
{
    auto model = QSharedPointer<ModelResult<DomainType, typename DomainType::Ptr> >::create(query, query.requestedProperties.toList());
    auto resultProvider = std::make_shared<ModelResultProvider<DomainType, typename DomainType::Ptr> >(model);
    //Keep the resultprovider alive for as long as the model lives
    model->setProperty("resultProvider", QVariant::fromValue(std::shared_ptr<void>(resultProvider)));

    // Query all resources and aggregate results
    KAsync::iterate(getResources(query.resources, ApplicationDomain::getTypeName<DomainType>()))
    .template each<void, QByteArray>([query, resultProvider](const QByteArray &resource, KAsync::Future<void> &future) {
        auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceName(resource), resource);
        if (facade) {
            facade->load(query, *resultProvider).template then<void>([&future](){future.setFinished();}).exec();
            //Keep the facade alive for the lifetime of the resultSet.
            //FIXME this would have to become a list
            resultProvider->setFacade(facade);
        } else {
            //Ignore the error and carry on
            future.setFinished();
        }
    }).template then<void>([query, resultProvider]() {
        resultProvider->initialResultSetComplete();
        if (!query.liveQuery) {
            resultProvider->complete();
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
    Trace() << "shutdown";
    return ResourceAccess::connectToServer(identifier).then<void, QSharedPointer<QLocalSocket>>([identifier](QSharedPointer<QLocalSocket> socket, KAsync::Future<void> &future) {
        //We can't currently reuse the socket
        socket->close();
        auto resourceAccess = QSharedPointer<Akonadi2::ResourceAccess>::create(identifier);
        resourceAccess->open();
        resourceAccess->sendCommand(Akonadi2::Commands::ShutdownCommand).then<void>([&future, resourceAccess]() {
            future.setFinished();
        }).exec();
    },
    [](int, const QString &) {
        //Resource isn't started, nothing to shutdown
    })
    //FIXME JOBAPI this is only required because we don't care about the return value of connectToServer
    .template then<void>([](){});
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
    template QSharedPointer<ResultEmitter<typename T::Ptr> > Store::load<T>(Query query); \
    template QSharedPointer<QAbstractItemModel> Store::loadModel<T>(Query query); \

REGISTER_TYPE(ApplicationDomain::Event);
REGISTER_TYPE(ApplicationDomain::Mail);
REGISTER_TYPE(ApplicationDomain::Folder);
REGISTER_TYPE(ApplicationDomain::AkonadiResource);

} // namespace Akonadi2

