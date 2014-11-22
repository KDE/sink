#pragma once

#include <QString>
#include <QSet>

namespace ClientAPI {

template<class T>
class Set {

};

template<class T>
class TreeSet : public Set<T> {

};

class DomainObject {

};

/*
 * Resource and domain object specific
 * FIXME: should we hardcode the requirement that the domain adapter is a subclass for the domain object?
 * * how do we allow copying of domain objects?
 * ** dummy domain object that is a wrapper?
 * ** domain adapter has an accessor for the domain object to hide subclassing
 */
class DomainAdapter : public DomainObject {
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
 * A query that matches a set of objects
 */
class Query
{
public:
    QSet<QString> resources() const { return QSet<QString>(); }
};

/**
 * Interface for the store facade
 */
template<class DomainType>
class StoreFacade {
public:
    virtual void create(const DomainType &domainObject) = 0;
    virtual void modify(const DomainType &domainObject) = 0;
    virtual void remove(const DomainType &domainObject) = 0;
    virtual void load(const Query &query) = 0;
};


class ResourceImpl {

};

/**
 * Actual implementation of the store facade that is provided by the resource plugin.
 *
 * It knows the buffer type used by the resource as well as the actual store used.
 * 
 * A resource must provide this facade for each domain type it knows.
 * => is reimplemented a lot
 * 
 * This interface should be executed in a thread so we can synchronously retrieve data from the store.
 */
template<class DomainType>
class StoreFacadeImpl : public StoreFacade<DomainType> {
public:
    void create(const DomainType &domainObject) {
        //FIXME here we would need to cast to DomainAdapter
        //Do actual work
    }

    void modify(const DomainType &domainObject) {
        //Do actual work
    }

    void remove(const DomainType &domainObject) {
        //Do actual work
    }

    Set<DomainType> load(Query) {
        Set<DomainType> resultSet;

        //retrieve results from store and fill into result set

        resultSet << 

        return resultSet;
    }

private:
    ResourceImpl resource;
};

/**
 * Facade factory that returns a store facade implementation, by loading a plugin and providing the relevant implementation.
 */
class FacadeFactory {
public:
    template<class DomainType>
    static StoreFacade<DomainType> getFacade(const QString &resource)
    {
        if (resource == "resourceX") {
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
    template <class DomainType>
    static Set<DomainType> load(Query)
    {
        //Query all resources and aggregate results
        //Query tells us in which resources we're interested
        Set<DomainType> resultSet;

        //FIXME this should run in a thread.
        //The result set is immediately returned and a "promise"/"resultprovider",
        //is passed to the actual query. The resultset is threadsafe so the query thread can safely move data
        //via the promise to the mainthread.
        for(auto resource, query.resources()) {
            auto facade = FacadeFactory::getFacade(resource);
            resultSet += facade.load<DomainType>(query);
        }
        return resultSet;
    }

    //Future load(id); => Set with single value

    template <class DomainType>
    static TreeSet<DomainType> loadTree(Query)
    {

    }

    //Sync methods for modifications
    template <class DomainType>
    static void create(const DomainType &domainObject) {
        auto facade = FacadeFactory::getFacade(domainObject.resource());
        facade.create(domainObject);
    }

    template <class DomainType>
    static void modify(const DomainType &domainObject) {
        auto facade = FacadeFactory::getFacade(domainObject.resource());
        facade.modify(domainObject);
    }

    template <class DomainType>
    static void remove(const DomainType &domainObject) {
        auto facade = FacadeFactory::getFacade(domainObject.resource());
        facade.remove(domainObject);
    }
};

}
