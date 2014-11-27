#pragma once

#include <QString>
#include <QSet>
#include <QSharedPointer>
#include <functional>
#include "store/database.h"

namespace ClientAPI {

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

class AkonadiDomainType {
    /*
     * Each domain object needs to store the resource, identifier, revision triple so we can link back to the storage location.
     */
    QString identifier;
    QString resource;
    qint64 revision;
};

class Event : public AkonadiDomainType {

};
class Todo : public AkonadiDomainType {

};
class Calendar : public AkonadiDomainType {

};
class Mail : public AkonadiDomainType {

};
class Folder : public AkonadiDomainType {

};

/*
 * Resource and domain object specific
 * FIXME: should we hardcode the requirement that the domain adapter is a subclass for the domain object?
 * * how do we allow copying of domain objects?
 * ** dummy domain object that is a wrapper?
 * ** domain adapter has an accessor for the domain object to hide subclassing
 */
class EventDomainAdapter : public Event {
    // virtual void setFoo(const QString &value)
    // {
    //     mBuffer.setFoo(value);
    // }

    // virtual QString foo() const
    // {
    //     return mBuffer.foo();
    // }

    // MessageBuffer mBuffer;
};



/**
 * Query result set
 *
 * This should probably become part of a generic kasync library.
 *
 * Functional is nice because we don't have to store data in the emitter
 * Non functional and storing may be the right thing because we want an in-memory representation of the set
 * non-functional also allows us to batch move data across thread boundaries.
 */

template<class T>
class ResultEmitter;

/*
 * The promise side for the result provider
 */
template<class T>
class ResultProvider {
public:
    void add(const T &value)
    {
        //the handler will be called in the other thread, protect
        mResultEmitter->addHandler(value);
    }

    QSharedPointer<ResultEmitter<T> > emitter()
    {
        mResultEmitter = QSharedPointer<ResultEmitter<T> >(new ResultEmitter<T>());
        return emitter;
    }

private:
    QSharedPointer<ResultEmitter<T> > mResultEmitter;
};

/*
 * The future side for the client.
 *
 * It does not directly hold the state.
 */
template<class T>
class ResultEmitter {
public:
    void onAdded(const std::function<void(const T&)> &handler);
    // void onRemoved(const std::function<void(const T&)> &handler);

private:
    friend class SetSource;
    std::function<void(const T&)> addHandler;
    // std::function<void(const T&)> removeHandler;
};

// template<class T>
// class TreeSet : public Set<T> {
// 
// };



/**
 * A query that matches a set of objects
 * 
 * The query will have to be updated regularly similary to the domain objects.
 * It probably also makes sense to have a domain specific part of the query,
 * such as what properties we're interested in (necessary information for on-demand
 * loading of data).
 */
class Query
{
public:
    //Resources to search
    QSet<QString> resources() const { return QSet<QString>(); }
};


/**
 * Interface for the store facade
 * 
 * All methods are synchronous.
 */
template<class DomainType>
class StoreFacade {
public:
    virtual void create(const DomainType &domainObject) = 0;
    virtual void modify(const DomainType &domainObject) = 0;
    virtual void remove(const DomainType &domainObject) = 0;
    virtual void load(const Query &query, const std::function<void(const DomainType &)> &resultCallback) = 0;
};


/**
 * Actual implementation of the store facade that is provided by the resource plugin.
 *
 * It knows the buffer type used by the resource as well as the actual store used.
 * 
 * A resource must provide this facade for each domain type it knows.
 * => is reimplemented a lot
 * => we should have a base implementation
 * 
 * This interface should be executed in a thread so we can synchronously retrieve data from the store.
 *
 * TODO: perhaps we should also allow async access and leave the thread/non-thread decision up to the implementation?
 */
template<typename DomainType>
class StoreFacadeImpl : public StoreFacade<Event> {
};

template<>
class StoreFacadeImpl<Event> : public StoreFacade<Event> {
public:
    void create(const Event &domainObject) {
        //FIXME here we would need to cast to DomainAdapter
        //Do actual work
        //transformFromDomainType(domainObject);
        //Ideally we have an adapter
        //getAdater(domainObject).buffer();
        //domainObject.key(); => The domain object needs to provide the id
        //writeToDb();
    }

    void modify(const Event &domainObject) {
        //Do actual work
    }

    void remove(const Event &domainObject) {
        //Do actual work
    }

    class EventBuffer {
        QString value;
    };

    static Event transformToDomainType(const EventBuffer &buffer) {
        //We may want to avoid copies here
        Event event;
        // //Ideally we don't have to copy and can use an adaptor instead
        // return DomainAdaptor
        return event;
    };

    void load(const Query &query, const std::function<void(const Event &)> &resultCallback) {
        //retrieve buffers from storage
        QList<EventBuffer> queryresult;
        foreach(const EventBuffer &buffer, queryresult) {
            resultCallback(transformToDomainType(buffer));
        }
    }

private:
    //Dummy implementation
    class ResourceImpl {};
    ResourceImpl resource;
    Database mDb;
};

/**
 * Facade factory that returns a store facade implementation, by loading a plugin and providing the relevant implementation.
 * 
 * If we were to provide default implementations for certain capabilities. Here would be the place to do so.
 * 
 * TODO: pluginmechansims for resources to provide their implementations.
 */
class FacadeFactory {
public:
    template<class DomainType>
    static StoreFacade<DomainType> getFacade(const QString &resource)
    {
        //TODO errorhandling in case the resource doesn't support the domain type
        if (resource == "dummyresource") {
            return StoreFacadeImpl<DomainType>();
        }
        return StoreFacadeImpl<DomainType>();
    }
};

/**
 * Store interface used in the client API
 */
class Store {
public:
    /**
     * Asynchronusly load a dataset
     */
    template <class DomainType>
    static QSharedPointer<ResultEmitter<DomainType> > load(Query query)
    {
        QSharedPointer<ResultProvider<DomainType> > resultSet(new ResultProvider<DomainType>);

        //Create a job that executes the search function.
        //We must guarantee that the emitter is returned before the first result is emitted.
        //The thread boundary handling is implemented in the result provider.
        // QtConcurrent::run([provider, resultSet](){
        //     // Query all resources and aggregate results
        //     // query tells us in which resources we're interested
        //     for(const auto &resource, query.resources()) {
        //         auto facade = FacadeFactory::getFacade(resource);
        //         facade.load<DomainType>(query, resultSet.add);
        //     }
        // });
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
        auto facade = FacadeFactory::getFacade<DomainType>(resourceIdentifier);
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
        auto facade = FacadeFactory::getFacade<DomainType>(resourceIdentifier);
        facade.modify(domainObject);
    }

    /**
     * Remove an entity.
     */
    template <class DomainType>
    static void remove(const DomainType &domainObject, const QString &resourceIdentifier) {
        //Potentially move to separate thread as well
        auto facade = FacadeFactory::getFacade<DomainType>(resourceIdentifier);
        facade.remove(domainObject);
    }
};

}
