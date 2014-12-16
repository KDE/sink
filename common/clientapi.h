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
#include <QTimer>
#include <QDebug>
#include <QEventLoop>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>
#include "threadboundary.h"

namespace async {
    //This should abstract if we execute from eventloop or in thread.
    //It supposed to allow the caller to finish the current method before executing the runner.
    void run(const std::function<void()> &runner) {
        QtConcurrent::run(runner);

        // //FIXME we should be using a Job instead of a timer
        // auto timer = new QTimer;
        // timer->setSingleShot(true);
        // QObject::connect(timer, &QTimer::timeout, runner);
        // QObject::connect(timer, &QTimer::timeout, timer, &QObject::deleteLater);
        // timer->start(0);
    };

    /**
    * Query result set
    */

    template<class T>
    class ResultEmitter;

    /*
    * The promise side for the result emitter
    */
    template<class T>
    class ResultProvider {
    public:
        //Called from worker thread
        void add(const T &value)
        {
            //We use the eventloop to call the addHandler directly from the main eventloop.
            //That way the result emitter implementation doesn't have to care about threadsafety at all.
            //The alternative would be to make all handlers of the emitter threadsafe.
            auto emitter = mResultEmitter;
            mResultEmitter->mThreadBoundary.callInMainThread([emitter, value]() {
                if (emitter) {
                    emitter->addHandler(value);
                }
            });
        }

        //Called from worker thread
        void complete()
        {
            auto emitter = mResultEmitter;
            mResultEmitter->mThreadBoundary.callInMainThread([emitter]() {
                if (emitter) {
                    emitter->completeHandler();
                }
            });
        }

        QSharedPointer<ResultEmitter<T> > emitter()
        {
            mResultEmitter = QSharedPointer<ResultEmitter<T> >(new ResultEmitter<T>());
            return mResultEmitter;
        }

    private:
        QSharedPointer<ResultEmitter<T> > mResultEmitter;
    };

    /*
    * The future side for the client.
    *
    * It does not directly hold the state.
    *
    * The advantage of this is that we can specialize it to:
    * * do inline transformations to the data
    * * directly store the state in a suitable datastructure: QList, QSet, std::list, QVector, ...
    * * build async interfaces with signals
    * * build sync interfaces that block when accessing the value
    *
    * TODO: This should probably be merged with daniels futurebase used in Async
    */
    template<class DomainType>
    class ResultEmitter {
    public:
        void onAdded(const std::function<void(const DomainType&)> &handler)
        {
            addHandler = handler;
        }
        // void onRemoved(const std::function<void(const T&)> &handler);
        void onComplete(const std::function<void(void)> &handler)
        {
            completeHandler = handler;
        }

    private:
        friend class ResultProvider<DomainType>;
        std::function<void(const DomainType&)> addHandler;
        // std::function<void(const T&)> removeHandler;
        std::function<void(void)> completeHandler;
        ThreadBoundary mThreadBoundary;
    };


    /*
     * A result set specialization that provides a syncronous list
     */
    template<class T>
    class SyncListResult : public QList<T> {
    public:
        SyncListResult(const QSharedPointer<ResultEmitter<T> > &emitter)
            :QList<T>(),
            mComplete(false),
            mEmitter(emitter)
        {
            emitter->onAdded([this](const T &value) {
                this->append(value);
            });
            emitter->onComplete([this]() {
                mComplete = true;
                auto loop = mWaitLoop.toStrongRef();
                if (loop) {
                    loop->quit();
                }
            });
        }

        void exec()
        {
            auto loop = QSharedPointer<QEventLoop>::create();
            mWaitLoop = loop;
            loop->exec(QEventLoop::ExcludeUserInputEvents);
        }

    private:
        bool mComplete;
        QWeakPointer<QEventLoop> mWaitLoop;
        QSharedPointer<ResultEmitter<T> > mEmitter;
    };
}

namespace Akonadi2 {

/**
 * Standardized Domain Types
 *
 * The don't adhere to any standard and can be freely extended
 * Their sole purpose is providing a standardized interface to access data.
 * 
 * This is necessary to decouple resource-backends from application domain containers (otherwise each resource would have to provide a faceade for each application domain container).
 *
 * These types will be frequently modified (for every new feature that should be exposed to the any client)
 */
namespace Domain {

class AkonadiDomainType {
public:
    AkonadiDomainType(const QString &resourceName, const QString &identifier, qint64 revision)
        : mResourceName(resourceName),
        mIdentifier(identifier),
        mRevision(revision)
    {
    }

    virtual QVariant getProperty(const QString &key){ return QVariant(); }

private:
    /*
     * Each domain object needs to store the resource, identifier, revision triple so we can link back to the storage location.
     */
    QString mResourceName;
    QString mIdentifier;
    qint64 mRevision;
};

class Event : public AkonadiDomainType {
public:
    typedef QSharedPointer<Event> Ptr;
    Event(const QString &resource, const QString &identifier, qint64 revision):AkonadiDomainType(resource, identifier, revision){};

};

class Todo : public AkonadiDomainType {
public:
    typedef QSharedPointer<Todo> Ptr;
};

class Calendar : public AkonadiDomainType {
public:
    typedef QSharedPointer<Calendar> Ptr;
};

class Mail : public AkonadiDomainType {
};

class Folder : public AkonadiDomainType {
};

/**
 * All types need to be registered here an MUST return a different name.
 * 
 * Do not store these types to disk, they may change over time.
 */

template<class DomainType>
QString getTypeName();

template<>
QString getTypeName<Event>()
{
    return "event";
}

template<>
QString getTypeName<Todo>()
{
    return "todo";
}

}

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
 */
class Query
{
public:
    //Could also be a propertyFilter
    QStringList resources;
    //Could also be a propertyFilter
    QStringList ids;
    //Filters to apply
    QHash<QString, QVariant> propertyFilter;
    //Properties to retrieve
    QSet<QString> requestedProperties;
};


/**
 * Interface for the store facade.
 * 
 * All methods are synchronous.
 * Facades are stateful (they hold connections to resources and database).
 * 
 * TODO: would it make sense to split the write, read and notification parts? (we could potentially save some connections)
 */
template<class DomainType>
class StoreFacade {
public:
    virtual ~StoreFacade(){};
    virtual void create(const DomainType &domainObject) = 0;
    virtual void modify(const DomainType &domainObject) = 0;
    virtual void remove(const DomainType &domainObject) = 0;
    virtual void load(const Query &query, const std::function<void(const typename DomainType::Ptr &)> &resultCallback) = 0;
};


/**
 * Facade factory that returns a store facade implementation, by loading a plugin and providing the relevant implementation.
 * 
 * If we were to provide default implementations for certain capabilities. Here would be the place to do so.
 * 
 * TODO: pluginmechansims for resources to provide their implementations.
 * * We may want a way to recycle facades to avoid recreating socket connections all the time?
 */

class FacadeFactory {
public:
    //FIXME: proper singleton implementation
    static FacadeFactory &instance()
    {
        static FacadeFactory factory;
        return factory;
    }

    static QString key(const QString &resource, const QString &type)
    {
        return resource + type;
    }

    template<class DomainType, class Facade>
    void registerFacade(const QString &resource)
    {
        const QString typeName = Domain::getTypeName<DomainType>();
        mFacadeRegistry.insert(key(resource, typeName), [](){ return new Facade; });
    }

    /*
     * Allows the registrar to register a specific instance.
     *
     * Primarily for testing.
     * The facade factory takes ovnership of the poniter and typically deletes the instance via shared pointer.
     * Supplied factory functions should therefore always return a new pointer (i.e. via clone())
     *
     * FIXME the factory function should really be returning QSharedPointer<void>, which doesn't work (std::shared_pointer<void> would though). That way i.e. a test could keep the object alive until it's done.
     */
    template<class DomainType, class Facade>
    void registerFacade(const QString &resource, const std::function<void*(void)> &customFactoryFunction)
    {
        const QString typeName = Domain::getTypeName<DomainType>();
        mFacadeRegistry.insert(key(resource, typeName), customFactoryFunction);
    }

    template<class DomainType>
    QSharedPointer<StoreFacade<DomainType> > getFacade(const QString &resource)
    {
        const QString typeName = Domain::getTypeName<DomainType>();
        auto factoryFunction = mFacadeRegistry.value(key(resource, typeName));
        if (factoryFunction) {
            return QSharedPointer<StoreFacade<DomainType> >(static_cast<StoreFacade<DomainType>* >(factoryFunction()));
        }
        qWarning() << "Failed to find facade for resource: " << resource << " and type: " << typeName;
        return QSharedPointer<StoreFacade<DomainType> >();
    }

private:
    QHash<QString, std::function<void*(void)> > mFacadeRegistry;
};

/**
 * Store interface used in the client API.
 *
 * TODO: For testing we need to be able to inject dummy StoreFacades. Should we work with a store instance, or a singleton factory?
 */
class Store {
public:
    static QString storageLocation()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2";
    }

    /**
     * Asynchronusly load a dataset
     */
    template <class DomainType>
    static QSharedPointer<ResultEmitter<typename DomainType::Ptr> > load(Query query)
    {
        QSharedPointer<ResultProvider<typename DomainType::Ptr> > resultSet(new ResultProvider<typename DomainType::Ptr>);

        //Execute the search in a thread.
        //We must guarantee that the emitter is returned before the first result is emitted.
        //The result provider must be threadsafe.
        async::run([resultSet, query](){
            // Query all resources and aggregate results
            // query tells us in which resources we're interested
            // TODO: queries to individual resources could be parallelized
            for(const QString &resource : query.resources) {
                auto facade = FacadeFactory::instance().getFacade<DomainType>(resource);
                //We have to bind an instance to the function callback. Since we use a shared pointer this keeps the result provider instance (and thus also the emitter) alive.
                std::function<void(const typename DomainType::Ptr &)> addCallback = std::bind(&ResultProvider<typename DomainType::Ptr>::add, resultSet, std::placeholders::_1);
                facade->load(query, addCallback);
            }
            resultSet->complete();
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
    static void create(const DomainType &domainObject, const QString &resourceIdentifier) {
        //Potentially move to separate thread as well
        auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceIdentifier);
        facade.create(domainObject);
    }

    /**
     * Modify an entity.
     * 
     * This includes moving etc. since these are also simple settings on a property.
     */
    template <class DomainType>
    static void modify(const DomainType &domainObject, const QString &resourceIdentifier) {
        //Potentially move to separate thread as well
        auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceIdentifier);
        facade.modify(domainObject);
    }

    /**
     * Remove an entity.
     */
    template <class DomainType>
    static void remove(const DomainType &domainObject, const QString &resourceIdentifier) {
        //Potentially move to separate thread as well
        auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceIdentifier);
        facade.remove(domainObject);
    }
};

}

