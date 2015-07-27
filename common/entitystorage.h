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

#include "clientapi.h"

#include <QByteArray>

#include "domainadaptor.h"
#include "entitybuffer.h"
#include "log.h"
#include "storage.h"
#include "resultset.h"

/**
 * Wraps storage, entity adaptor factory and indexes into one.
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
    virtual ResultSet queryIndexes(const Akonadi2::Query &query, const QByteArray &resourceInstanceIdentifier, QSet<QByteArray> &appliedFilters) = 0;

    void readValue(const QSharedPointer<Akonadi2::Storage> &storage, const QByteArray &key, const std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &)> &resultCallback);
    ResultSet filteredSet(const ResultSet &resultSet, const std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter, const QSharedPointer<Akonadi2::Storage> &storage, qint64 baseRevision, qint64 topRevision);
    ResultSet getResultSet(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::Storage> &storage, qint64 baseRevision, qint64 topRevision);

protected:
    QByteArray mResourceInstanceIdentifier;
    DomainTypeAdaptorFactoryInterface::Ptr mDomainTypeAdaptorFactory;
};

template<typename DomainType>
class EntityStorage : public EntityStorageBase
{

public:
    EntityStorage(const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory)
        : EntityStorageBase(instanceIdentifier, adaptorFactory)
    {

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

    ResultSet queryIndexes(const Akonadi2::Query &query, const QByteArray &resourceInstanceIdentifier, QSet<QByteArray> &appliedFilters) Q_DECL_OVERRIDE
    {
        return Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::queryIndexes(query, resourceInstanceIdentifier, appliedFilters);
    }

public:

    virtual void read(const Akonadi2::Query &query, const QPair<qint64, qint64> &revisionRange, const QSharedPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > &resultProvider) 
    {
        auto storage = QSharedPointer<Akonadi2::Storage>::create(Akonadi2::Store::storageLocation(), mResourceInstanceIdentifier);
        storage->setDefaultErrorHandler([](const Akonadi2::Storage::Error &error) {
            Warning() << "Error during query: " << error.store << error.message;
        });

        storage->startTransaction(Akonadi2::Storage::ReadOnly);
        //TODO start transaction on indexes as well

        Log() << "Querying" << revisionRange.first << revisionRange.second;
        auto resultSet = getResultSet(query, storage, revisionRange.first, revisionRange.second);
        while(resultSet.next([this, resultProvider](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &value) -> bool {
            auto cloned = copy(*value);
            resultProvider->add(cloned.template staticCast<DomainType>());
            return true;
        })){};
        //TODO replay removals and modifications
        storage->abortTransaction();
    }

};
