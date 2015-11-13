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

#pragma once

#include <QString>
#include <QSharedPointer>
#include <QEventLoop>
#include <QAbstractItemModel>
#include <functional>
#include <memory>

#include <Async/Async>

#include "query.h"
#include "resultprovider.h"
#include "applicationdomaintype.h"
#include "facadefactory.h"
#include "log.h"

namespace async {
    //This should abstract if we execute from eventloop or in thread.
    //It supposed to allow the caller to finish the current method before executing the runner.
    void run(const std::function<void()> &runner);
}

namespace Akonadi2 {

using namespace async;

/**
 * Store interface used in the client API.
 */
class Store {
private:
    static QList<QByteArray> getResources(const QList<QByteArray> &resourceFilter, const QByteArray &type);

public:
    static QString storageLocation();
    static QByteArray resourceName(const QByteArray &instanceIdentifier);

    /**
     * Asynchronusly load a dataset
     */
    template <class DomainType>
    static QSharedPointer<ResultEmitter<typename DomainType::Ptr> > load(Query query)
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
                auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceName(resource), resource);
                if (facade) {
                    facade->load(query, resultSet).template then<void>([&future](){future.setFinished();}).exec();
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

    /**
     * Asynchronusly load a dataset with tree structure information
     */
    template <class DomainType>
    static QSharedPointer<QAbstractItemModel> loadModel(Query query)
    {
        auto model = QSharedPointer<ModelResult<DomainType, typename DomainType::Ptr> >::create(query, QList<QByteArray>() << "summary" << "uid");
        auto resultProvider = QSharedPointer<ModelResultProvider<DomainType, typename DomainType::Ptr> >::create(model);

        // Query all resources and aggregate results
        KAsync::iterate(getResources(query.resources, ApplicationDomain::getTypeName<DomainType>()))
        .template each<void, QByteArray>([query, resultProvider](const QByteArray &resource, KAsync::Future<void> &future) {
            auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceName(resource), resource);
            if (facade) {
                facade->load(query, resultProvider).template then<void>([&future](){future.setFinished();}).exec();
                //Keep the facade alive for the lifetime of the resultSet.
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

    /**
     * Create a new entity.
     */
    template <class DomainType>
    static KAsync::Job<void> create(const DomainType &domainObject) {
        //Potentially move to separate thread as well
        auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
        return facade->create(domainObject).template then<void>([facade](){}, [](int errorCode, const QString &error) {
            Warning() << "Failed to create";
        });
    }

    /**
     * Modify an entity.
     * 
     * This includes moving etc. since these are also simple settings on a property.
     */
    template <class DomainType>
    static KAsync::Job<void> modify(const DomainType &domainObject) {
        //Potentially move to separate thread as well
        auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
        return facade->modify(domainObject).template then<void>([facade](){}, [](int errorCode, const QString &error) {
            Warning() << "Failed to modify";
        });
    }

    /**
     * Remove an entity.
     */
    template <class DomainType>
    static KAsync::Job<void> remove(const DomainType &domainObject) {
        //Potentially move to separate thread as well
        auto facade = getFacade<DomainType>(domainObject.resourceInstanceIdentifier());
        return facade->remove(domainObject).template then<void>([facade](){}, [](int errorCode, const QString &error) {
            Warning() << "Failed to remove";
        });
    }

    /**
     * Shutdown resource.
     */
    static KAsync::Job<void> shutdown(const QByteArray &resourceIdentifier);

    /**
     * Synchronize data to local cache.
     */
    static KAsync::Job<void> synchronize(const Akonadi2::Query &query);
};


}

