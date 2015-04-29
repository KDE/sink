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
#include <memory>
#include "threadboundary.h"
#include "async/src/async.h"

namespace async {
    //This should abstract if we execute from eventloop or in thread.
    //It supposed to allow the caller to finish the current method before executing the runner.
    void run(const std::function<void()> &runner);

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
    private:
        void callInMainThreadOnEmitter(void (ResultEmitter<T>::*f)())
        {
            //We use the eventloop to call the addHandler directly from the main eventloop.
            //That way the result emitter implementation doesn't have to care about threadsafety at all.
            //The alternative would be to make all handlers of the emitter threadsafe.
            if (auto emitter = mResultEmitter.toStrongRef()) {
                auto weakEmitter = mResultEmitter;
                //We don't want to keep the emitter alive here, so we only capture a weak reference
                emitter->mThreadBoundary.callInMainThread([weakEmitter, f]() {
                    if (auto strongRef = weakEmitter.toStrongRef()) {
                        (strongRef.data()->*f)();
                    }
                });
            }
        }

        void callInMainThreadOnEmitter(const std::function<void()> &f)
        {
            //We use the eventloop to call the addHandler directly from the main eventloop.
            //That way the result emitter implementation doesn't have to care about threadsafety at all.
            //The alternative would be to make all handlers of the emitter threadsafe.
            if (auto emitter = mResultEmitter.toStrongRef()) {
                emitter->mThreadBoundary.callInMainThread([f]() {
                    f();
                });
            }
        }

    public:
        //Called from worker thread
        void add(const T &value)
        {
            //Because I don't know how to use bind
            auto weakEmitter = mResultEmitter;
            callInMainThreadOnEmitter([weakEmitter, value](){
                if (auto strongRef = weakEmitter.toStrongRef()) {
                    strongRef->addHandler(value);
                }
            });
        }

        void initialResultSetComplete()
        {
            callInMainThreadOnEmitter(&ResultEmitter<T>::initialResultSetComplete);
        }

        //Called from worker thread
        void complete()
        {
            callInMainThreadOnEmitter(&ResultEmitter<T>::complete);
        }

        void clear()
        {
            callInMainThreadOnEmitter(&ResultEmitter<T>::clear);
        }


        QSharedPointer<ResultEmitter<T> > emitter()
        {
            if (!mResultEmitter) {
                //We have to go over a separate var and return that, otherwise we'd delete the emitter immediately again
                auto sharedPtr = QSharedPointer<ResultEmitter<T> >(new ResultEmitter<T>, [this](ResultEmitter<T> *emitter){ done(); delete emitter; });
                mResultEmitter = sharedPtr;
                return sharedPtr;
            }

            return mResultEmitter.toStrongRef();
        }

        /**
         * For lifetimemanagement only.
         * We keep the runner alive as long as the result provider exists.
         */
        void setQueryRunner(const QSharedPointer<QObject> &runner)
        {
            mQueryRunner = runner;
        }

        /**
         * For lifetimemanagement only.
         * We keep the runner alive as long as the result provider exists.
         */
        void setFacade(const std::shared_ptr<void> &facade)
        {
            mFacade = facade;
        }

        void onDone(const std::function<void()> &callback)
        {
            mOnDoneCallback = callback;
        }

        bool isDone() const
        {
            //The existance of the emitter currently defines wether we're done or not.
            return mResultEmitter.toStrongRef().isNull();
        }

    private:
        void done()
        {
            qWarning() << "done";
            if (mOnDoneCallback) {
                mOnDoneCallback();
                mOnDoneCallback = std::function<void()>();
            }
        }

        QWeakPointer<ResultEmitter<T> > mResultEmitter;
        QSharedPointer<QObject> mQueryRunner;
        std::shared_ptr<void> mFacade;
        std::function<void()> mOnDoneCallback;
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
    */
    template<class DomainType>
    class ResultEmitter {
    public:
        void onAdded(const std::function<void(const DomainType&)> &handler)
        {
            addHandler = handler;
        }
        // void onRemoved(const std::function<void(const T&)> &handler);
        void onInitialResultSetComplete(const std::function<void(void)> &handler)
        {
            initialResultSetCompleteHandler = handler;
        }
        void onComplete(const std::function<void(void)> &handler)
        {
            completeHandler = handler;
        }
        void onClear(const std::function<void(void)> &handler)
        {
            clearHandler = handler;
        }

        void add(const DomainType &value)
        {
            addHandler(value);
        }

        void initialResultSetComplete()
        {
            initialResultSetCompleteHandler();
        }

        void complete()
        {
            completeHandler();
        }

        void clear()
        {
            clearHandler();
        }

    private:
        friend class ResultProvider<DomainType>;

        std::function<void(const DomainType&)> addHandler;
        // std::function<void(const T&)> removeHandler;
        std::function<void(void)> initialResultSetCompleteHandler;
        std::function<void(void)> completeHandler;
        std::function<void(void)> clearHandler;
        ThreadBoundary mThreadBoundary;
    };

}

namespace Akonadi2 {

/**
 * Standardized Application Domain Types
 *
 * They don't adhere to any standard and can be freely extended
 * Their sole purpose is providing a standardized interface to access data.
 * 
 * This is necessary to decouple resource-backends from application domain containers (otherwise each resource would have to provide a faceade for each application domain container).
 *
 * These types will be frequently modified (for every new feature that should be exposed to the any client)
 */
namespace ApplicationDomain {

/**
 * This class has to be implemented by resources and can be used as generic interface to access the buffer properties
 */
class BufferAdaptor {
public:
    virtual ~BufferAdaptor() {}
    virtual QVariant getProperty(const QByteArray &key) const { return QVariant(); }
    virtual void setProperty(const QByteArray &key, const QVariant &value) {}
    virtual QList<QByteArray> availableProperties() const { return QList<QByteArray>(); }
};

class MemoryBufferAdaptor : public BufferAdaptor {
public:
    MemoryBufferAdaptor()
        : BufferAdaptor()
    {
    }

    MemoryBufferAdaptor(const BufferAdaptor &buffer)
        : BufferAdaptor()
    {
        for(const auto &property : buffer.availableProperties()) {
            mValues.insert(property, buffer.getProperty(property));
        }
    }

    virtual ~MemoryBufferAdaptor() {}

    virtual QVariant getProperty(const QByteArray &key) const { return mValues.value(key); }
    virtual void setProperty(const QByteArray &key, const QVariant &value) { mValues.insert(key, value); }
    virtual QByteArrayList availableProperties() const { return mValues.keys(); }

private:
    QHash<QByteArray, QVariant> mValues;
};

/**
 * The domain type interface has two purposes:
 * * provide a unified interface to read buffers (for zero-copy reading)
 * * record changes to generate changesets for modifications
 */
class ApplicationDomainType {
public:
    ApplicationDomainType()
        :mAdaptor(new MemoryBufferAdaptor())
    {

    }
    ApplicationDomainType(const QByteArray &resourceName, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor)
        : mAdaptor(adaptor),
        mResourceName(resourceName),
        mIdentifier(identifier),
        mRevision(revision)
    {
    }

    virtual ~ApplicationDomainType() {}

    virtual QVariant getProperty(const QByteArray &key) const { return mAdaptor->getProperty(key); }
    virtual void setProperty(const QByteArray &key, const QVariant &value){ mChangeSet.insert(key, value); mAdaptor->setProperty(key, value); }
    virtual QByteArrayList changedProperties() const { return mChangeSet.keys(); }
    qint64 revision() const { return mRevision; }

private:
    QSharedPointer<BufferAdaptor> mAdaptor;
    QHash<QByteArray, QVariant> mChangeSet;
    /*
     * Each domain object needs to store the resource, identifier, revision triple so we can link back to the storage location.
     */
    QString mResourceName;
    QByteArray mIdentifier;
    qint64 mRevision;
};

struct Event : public ApplicationDomainType {
    typedef QSharedPointer<Event> Ptr;
    using ApplicationDomainType::ApplicationDomainType;
};

struct Todo : public ApplicationDomainType {
    typedef QSharedPointer<Todo> Ptr;
    using ApplicationDomainType::ApplicationDomainType;
};

struct Calendar : public ApplicationDomainType {
    typedef QSharedPointer<Calendar> Ptr;
    using ApplicationDomainType::ApplicationDomainType;
};

class Mail : public ApplicationDomainType {
};

class Folder : public ApplicationDomainType {
};

/**
 * Represents an akonadi resource.
 * 
 * This type is used for configuration of resources,
 * and for creating and removing resource instances.
 */
struct AkonadiResource : public ApplicationDomainType {
    typedef QSharedPointer<AkonadiResource> Ptr;
    using ApplicationDomainType::ApplicationDomainType;
};

/**
 * All types need to be registered here an MUST return a different name.
 * 
 * Do not store these types to disk, they may change over time.
 */

template<class DomainType>
QByteArray getTypeName();

template<>
QByteArray getTypeName<Event>();

template<>
QByteArray getTypeName<Todo>();

template<>
QByteArray getTypeName<AkonadiResource>();

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
    QByteArray type() const { return ApplicationDomain::getTypeName<DomainType>(); }
    virtual Async::Job<void> create(const DomainType &domainObject) = 0;
    virtual Async::Job<void> modify(const DomainType &domainObject) = 0;
    virtual Async::Job<void> remove(const DomainType &domainObject) = 0;
    virtual Async::Job<void> load(const Query &query, const QSharedPointer<ResultProvider<typename DomainType::Ptr> > &resultProvider) = 0;
};


/**
 * Facade factory that returns a store facade implementation, by loading a plugin and providing the relevant implementation.
 *
 * If we were to provide default implementations for certain capabilities. Here would be the place to do so.
 */

class FacadeFactory {
public:
    typedef std::function<void*(bool &externallyManaged)> FactoryFunction;

    //FIXME: proper singleton implementation
    static FacadeFactory &instance()
    {
        static FacadeFactory factory;
        return factory;
    }

    static QByteArray key(const QByteArray &resource, const QByteArray &type)
    {
        return resource + type;
    }

    template<class DomainType, class Facade>
    void registerFacade(const QByteArray &resource)
    {
        const QByteArray typeName = ApplicationDomain::getTypeName<DomainType>();
        mFacadeRegistry.insert(key(resource, typeName), [](bool &externallyManaged){ return new Facade; });
    }

    /*
     * Allows the registrar to register a specific instance.
     *
     * Primarily for testing.
     * The facade factory takes ovnership of the pointer and typically deletes the instance via shared pointer.
     * Supplied factory functions should therefore always return a new pointer (i.e. via clone())
     *
     * FIXME the factory function should really be returning QSharedPointer<void>, which doesn't work (std::shared_pointer<void> would though). That way i.e. a test could keep the object alive until it's done. As a workaround the factory function can define wether it manages the lifetime of the facade itself.
     */
    template<class DomainType, class Facade>
    void registerFacade(const QByteArray &resource, const FactoryFunction &customFactoryFunction)
    {
        const QByteArray typeName = ApplicationDomain::getTypeName<DomainType>();
        mFacadeRegistry.insert(key(resource, typeName), customFactoryFunction);
    }

    /*
     * Can be used to clear the factory.
     *
     * Primarily for testing.
     */
    void resetFactory()
    {
        mFacadeRegistry.clear();
    }

    static void doNothingDeleter(void *)
    {
        qWarning() << "Do nothing";
    }

    template<class DomainType>
    std::shared_ptr<StoreFacade<DomainType> > getFacade(const QByteArray &resource)
    {
        const QByteArray typeName = ApplicationDomain::getTypeName<DomainType>();
        auto factoryFunction = mFacadeRegistry.value(key(resource, typeName));
        if (factoryFunction) {
            bool externallyManaged = false;
            auto ptr = static_cast<StoreFacade<DomainType>* >(factoryFunction(externallyManaged));
            if (externallyManaged) {
                //Allows tests to manage the lifetime of injected facades themselves
                return std::shared_ptr<StoreFacade<DomainType> >(ptr, doNothingDeleter);
            } else {
                return std::shared_ptr<StoreFacade<DomainType> >(ptr);
            }
        }
        qWarning() << "Failed to find facade for resource: " << resource << " and type: " << typeName;
        return std::shared_ptr<StoreFacade<DomainType> >();
    }

private:
    QHash<QByteArray, FactoryFunction> mFacadeRegistry;
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
            // Query all resources and aggregate results
            Async::iterate(query.resources)
            .template each<void, QByteArray>([query, resultSet](const QByteArray &resource, Async::Future<void> &future) {
                //TODO pass resource identifier to factory
                auto facade = FacadeFactory::instance().getFacade<DomainType>(resource);
                facade->load(query, resultSet).template then<void>([&future](){future.setFinished();}).exec();
                //Keep the facade alive for the lifetime of the resultSet.
                resultSet->setFacade(facade);
            }).template then<void>([query, resultSet]() {
                resultSet->initialResultSetComplete();
                if (!query.liveQuery) {
                    resultSet->complete();
                }
            }).exec();

            //Keep the thread alive until the result is ready
            if (!resultSet->isDone()) {
                QEventLoop eventLoop;
                resultSet->onDone([&eventLoop](){
                    eventLoop.quit();
                });
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
    //TODO return job that tracks progress until resource has stored the message in it's queue?
    template <class DomainType>
    static void create(const DomainType &domainObject, const QByteArray &resourceIdentifier) {
        //Potentially move to separate thread as well
        auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceIdentifier);
        facade->create(domainObject).exec().waitForFinished();
        //TODO return job?
    }

    /**
     * Modify an entity.
     * 
     * This includes moving etc. since these are also simple settings on a property.
     */
    template <class DomainType>
    static void modify(const DomainType &domainObject, const QByteArray &resourceIdentifier) {
        //Potentially move to separate thread as well
        auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceIdentifier);
        facade->modify(domainObject).exec().waitForFinished();
        //TODO return job?
    }

    /**
     * Remove an entity.
     */
    template <class DomainType>
    static void remove(const DomainType &domainObject, const QByteArray &resourceIdentifier) {
        //Potentially move to separate thread as well
        auto facade = FacadeFactory::instance().getFacade<DomainType>(resourceIdentifier);
        facade->remove(domainObject).exec().waitForFinished();
        //TODO return job?
    }

    static void shutdown(const QByteArray &resourceIdentifier);
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

