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
#include <QSet>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QDebug>
#include <QEventLoop>
#include <functional>
#include <memory>

#include <Async/Async>

#include "threadboundary.h"
#include "resultprovider.h"
#include "domain/applicationdomaintype.h"
#include "resourceconfig.h"
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
 * A query that matches a set of objects
 * 
 * The query will have to be updated regularly similary to the domain objects.
 * It probably also makes sense to have a domain specific part of the query,
 * such as what properties we're interested in (necessary information for on-demand
 * loading of data).
 *
 * The query defines:
 * * what resources to search
 * * filters on various properties (parent collection, startDate range, ....)
 * * properties we need (for on-demand querying)
 * 
 * syncOnDemand: Execute a source sync before executing the query
 * processAll: Ensure all local messages are processed before querying to guarantee an up-to date dataset.
 */
class Query
{
public:
    Query() : syncOnDemand(true), processAll(false), liveQuery(false) {}
    //Could also be a propertyFilter
    QByteArrayList resources;
    //Could also be a propertyFilter
    QByteArrayList ids;
    //Filters to apply
    QHash<QByteArray, QVariant> propertyFilter;
    //Properties to retrieve
    QSet<QByteArray> requestedProperties;
    bool syncOnDemand;
    bool processAll;
    //If live query is false, this query will not continuously be updated
    bool liveQuery;
};



/**
 * Store interface used in the client API.
 */
class Store {
public:
    static QString storageLocation()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/storage";
    }

    static QByteArray resourceName(const QByteArray &instanceIdentifier)
    {
        auto split = instanceIdentifier.split('.');
        if (split.size() <= 1) {
            return instanceIdentifier;
        }
        split.removeLast();
        return split.join('.');
    }

    static QList<QByteArray> getResources(const QList<QByteArray> &resourceFilter)
    {
        QList<QByteArray> resources;
        const auto configuredResources = ResourceConfig::getResources();
        if (resourceFilter.isEmpty()) {
            for (const auto &res : configuredResources) {
                //TODO filter by type
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
        return resources;
    }

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
            KAsync::iterate(getResources(query.resources))
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
    // template <class DomainType>
    // static TreeSet<DomainType> loadTree(Query)
    // {

    // }

    /**
     * Create a new entity.
     */
    template <class DomainType>
    static KAsync::Job<void> create(const DomainType &domainObject, const QByteArray &resourceIdentifier) {
        //Potentially move to separate thread as well
        auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceName(resourceIdentifier), resourceIdentifier);
        if (facade) {
            return facade->create(domainObject).template then<void>([facade](){}, [](int errorCode, const QString &error) {
                Warning() << "Failed to create";
            });
        }
        return KAsync::error<void>(-1, "Failed to create a facade");
    }

    /**
     * Modify an entity.
     * 
     * This includes moving etc. since these are also simple settings on a property.
     */
    template <class DomainType>
    static KAsync::Job<void> modify(const DomainType &domainObject, const QByteArray &resourceIdentifier) {
        //Potentially move to separate thread as well
        auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceName(resourceIdentifier), resourceIdentifier);
        if (facade) {
            return facade->modify(domainObject).template then<void>([facade](){}, [](int errorCode, const QString &error) {
                Warning() << "Failed to modify";
            });
        }
        return KAsync::error<void>(-1, "Failed to create a facade");
    }

    /**
     * Remove an entity.
     */
    template <class DomainType>
    static KAsync::Job<void> remove(const DomainType &domainObject, const QByteArray &resourceIdentifier) {
        //Potentially move to separate thread as well
        auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceName(resourceIdentifier), resourceIdentifier);
        if (facade) {
            facade->remove(domainObject).template then<void>([facade](){}, [](int errorCode, const QString &error) {
                Warning() << "Failed to remove";
            }).exec().waitForFinished();
        }
        return KAsync::error<void>(-1, "Failed to create a facade");
    }

    static void shutdown(const QByteArray &resourceIdentifier);

    //TODO do we really want this command? And if yes, shouldn't it take a query to specify what to sync exactly?
    static void synchronize(const QByteArray &resourceIdentifier);
};

/**
 * Configuration interface used in the client API.
 *
 * This interface provides convenience API for manipulating resources.
 * This interface uses internally the same interface that is part of the regular Store API.
 * 
 * Resources provide their configuration implementation by implementing a StoreFacade for the AkonadiResource type.
 */
class Configuration {
public:
    static QWidget *getConfigurationWidget(const QByteArray &resourceIdentifier)
    {
        //TODO here we want to implement the code to create a configuration widget from the QML config interface provided by the resource
        return nullptr;
    }

    static ApplicationDomain::AkonadiResource::Ptr getConfiguration(const QByteArray &resource)
    {
        Query query;
        query.resources << resource;
        // auto result = Store::load<ApplicationDomain::AkonadiResource>(query);
        //FIXME retrieve result and return it
        return ApplicationDomain::AkonadiResource::Ptr::create();
    }

    static void setConfiguration(const ApplicationDomain::AkonadiResource &resource, const QByteArray &resourceIdentifier)
    {
        Store::modify<ApplicationDomain::AkonadiResource>(resource, resourceIdentifier);
    }

    static void createResource(const ApplicationDomain::AkonadiResource &resource, const QByteArray &resourceIdentifier)
    {
        Store::create<ApplicationDomain::AkonadiResource>(resource, resourceIdentifier);
    }

    static void removeResource(const QByteArray &resourceIdentifier)
    {
        ApplicationDomain::AkonadiResource resource;
        Store::remove<ApplicationDomain::AkonadiResource>(resource, resourceIdentifier);
    }
};

}

