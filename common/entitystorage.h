/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */
#pragma once

#include <QByteArray>

#include "query.h"
#include "domainadaptor.h"
#include "entitybuffer.h"
#include "log.h"
#include "storage.h"
#include "resultset.h"
#include "resultprovider.h"
#include "definitions.h"

/**
 * Wraps storage, entity adaptor factory and indexes into one.
 * 
 * TODO: customize with readEntity instead of adaptor factory
 */
class EntityStorageBase
{
protected:
    EntityStorageBase(const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory)
        : mResourceInstanceIdentifier(instanceIdentifier),
        mDomainTypeAdaptorFactory(adaptorFactory)
    {

    }

    virtual Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr create(const QByteArray &key, qint64 revision, const QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor> &adaptor) = 0;
    virtual Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr copy(const Akonadi2::ApplicationDomain::ApplicationDomainType &) = 0;
    virtual ResultSet queryIndexes(const Akonadi2::Query &query, const QByteArray &resourceInstanceIdentifier, QSet<QByteArray> &appliedFilters, Akonadi2::Storage::Transaction &transaction) = 0;

    /**
     * Loads a single entity by uid from storage.
     * 
     * TODO: Resources should be able to customize this for cases where an entity is not the same as a single buffer.
     */
    void readEntity(const Akonadi2::Storage::Transaction &transaction, const QByteArray &key, const std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &, Akonadi2::Operation)> &resultCallback);
    ResultSet getResultSet(const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, qint64 baseRevision);

protected:
    QByteArray mResourceInstanceIdentifier;
    QByteArray mBufferType;
    DomainTypeAdaptorFactoryInterface::Ptr mDomainTypeAdaptorFactory;
private:
    /**
     * Returns the initial result set that still needs to be filtered.
     *
     * To make this efficient indexes should be chosen that are as selective as possible.
     */
    ResultSet loadInitialResultSet(const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters);
    ResultSet filteredSet(const ResultSet &resultSet, const std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter, const Akonadi2::Storage::Transaction &transaction, bool isInitialQuery);
};

template<typename DomainType>
class EntityStorage : public EntityStorageBase
{

public:
    EntityStorage(const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory, const QByteArray &bufferType)
        : EntityStorageBase(instanceIdentifier, adaptorFactory)
    {
        mBufferType = bufferType;
    }

protected:
    Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr create(const QByteArray &key, qint64 revision, const QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor> &adaptor) Q_DECL_OVERRIDE
    {
        return DomainType::Ptr::create(mResourceInstanceIdentifier, key, revision, adaptor);
    }

    Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr copy(const Akonadi2::ApplicationDomain::ApplicationDomainType &object) Q_DECL_OVERRIDE
    {
        return Akonadi2::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<DomainType>(object);
    }

    ResultSet queryIndexes(const Akonadi2::Query &query, const QByteArray &resourceInstanceIdentifier, QSet<QByteArray> &appliedFilters, Akonadi2::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        return Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::queryIndexes(query, resourceInstanceIdentifier, appliedFilters, transaction);
    }

public:

    virtual qint64 read(const Akonadi2::Query &query, qint64 baseRevision, const QSharedPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > &resultProvider)
    {
        Akonadi2::Storage storage(Akonadi2::storageLocation(), mResourceInstanceIdentifier);
        storage.setDefaultErrorHandler([](const Akonadi2::Storage::Error &error) {
            Warning() << "Error during query: " << error.store << error.message;
        });

        auto transaction = storage.createTransaction(Akonadi2::Storage::ReadOnly);

        Log() << "Querying" << baseRevision;
        auto resultSet = getResultSet(query, transaction, baseRevision);
        while(resultSet.next([this, resultProvider](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &value, Akonadi2::Operation operation) -> bool {
            switch (operation) {
            case Akonadi2::Operation_Creation:
                Trace() << "Got creation";
                resultProvider->add(copy(*value).template staticCast<DomainType>());
                break;
            case Akonadi2::Operation_Modification:
                Trace() << "Got modification";
                resultProvider->modify(copy(*value).template staticCast<DomainType>());
                break;
            case Akonadi2::Operation_Removal:
                Trace() << "Got removal";
                resultProvider->remove(copy(*value).template staticCast<DomainType>());
                break;
            }
            return true;
        })){};
        return Akonadi2::Storage::maxRevision(transaction);
    }

};
