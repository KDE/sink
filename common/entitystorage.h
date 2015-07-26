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
template <typename DomainType>
class EntityStorage
{

public:
    EntityStorage(const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory)
        : mResourceInstanceIdentifier(instanceIdentifier),
        mDomainTypeAdaptorFactory(adaptorFactory)
    {

    }

private:
    static void scan(const QSharedPointer<Akonadi2::Storage> &storage, const QByteArray &key, std::function<bool(const QByteArray &key, const Akonadi2::Entity &entity)> callback)
    {
        storage->scan(key, [=](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
            //Skip internals
            if (Akonadi2::Storage::isInternalKey(keyValue, keySize)) {
                return true;
            }

            //Extract buffers
            Akonadi2::EntityBuffer buffer(dataValue, dataSize);

            //FIXME implement buffer.isValid()
            // const auto resourceBuffer = Akonadi2::EntityBuffer::readBuffer<DummyEvent>(buffer.entity().resource());
            // const auto localBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::ApplicationDomain::Buffer::Event>(buffer.entity().local());
            // const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(buffer.entity().metadata());

            // if ((!resourceBuffer && !localBuffer) || !metadataBuffer) {
            //     qWarning() << "invalid buffer " << QByteArray::fromRawData(static_cast<char*>(keyValue), keySize);
            //     return true;
            // }
            return callback(QByteArray::fromRawData(static_cast<char*>(keyValue), keySize), buffer.entity());
        },
        [](const Akonadi2::Storage::Error &error) {
            qWarning() << "Error during query: " << error.message;
        });
    }

    static void readValue(const QSharedPointer<Akonadi2::Storage> &storage, const QByteArray &key, const std::function<void(const typename DomainType::Ptr &)> &resultCallback, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory, const QByteArray &instanceIdentifier)
    {
        scan(storage, key, [=](const QByteArray &key, const Akonadi2::Entity &entity) {
            const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(entity.metadata());
            qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
            //This only works for a 1:1 mapping of resource to domain types.
            //Not i.e. for tags that are stored as flags in each entity of an imap store.
            //additional properties that don't have a 1:1 mapping (such as separately stored tags),
            //could be added to the adaptor
            auto domainObject = QSharedPointer<DomainType>::create(instanceIdentifier, key, revision, adaptorFactory->createAdaptor(entity));
            resultCallback(domainObject);
            return true;
        });
    }

    static ResultSet fullScan(const QSharedPointer<Akonadi2::Storage> &storage)
    {
        //TODO use a result set with an iterator, to read values on demand
        QVector<QByteArray> keys;
        scan(storage, QByteArray(), [=, &keys](const QByteArray &key, const Akonadi2::Entity &) {
            keys << key;
            return true;
        });
        Trace() << "Full scan found " << keys.size() << " results";
        return ResultSet(keys);
    }

    static ResultSet filteredSet(const ResultSet &resultSet, const std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter, const QSharedPointer<Akonadi2::Storage> &storage, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory, qint64 baseRevision, qint64 topRevision, const QByteArray &instanceIdentifier)
    {
        auto resultSetPtr = QSharedPointer<ResultSet>::create(resultSet);

        //Read through the source values and return whatever matches the filter
        std::function<bool(std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &)>)> generator = [resultSetPtr, storage, adaptorFactory, filter, instanceIdentifier](std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &)> callback) -> bool {
            while (resultSetPtr->next()) {
                readValue(storage, resultSetPtr->id(), [filter, callback](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject) {
                    if (filter(domainObject)) {
                        callback(domainObject);
                    }
                }, adaptorFactory, instanceIdentifier);
            }
            return false;
        };
        return ResultSet(generator);
    }

    static ResultSet getResultSet(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::Storage> &storage, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory, const QByteArray &resourceInstanceIdentifier, qint64 baseRevision, qint64 topRevision)
    {
        QSet<QByteArray> appliedFilters;
        ResultSet resultSet = Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::queryIndexes(query, resourceInstanceIdentifier, appliedFilters, qMakePair(baseRevision, topRevision));
        const auto remainingFilters = query.propertyFilter.keys().toSet() - appliedFilters;

        //We do a full scan if there were no indexes available to create the initial set.
        if (appliedFilters.isEmpty()) {
            resultSet = fullScan(storage);
        }

        auto filter = [remainingFilters, query, baseRevision, topRevision](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject) -> bool {
            if (topRevision > 0) {
                Trace() << "filtering by revision " << domainObject->revision();
                if (domainObject->revision() < baseRevision || domainObject->revision() > topRevision) {
                    return false;
                }
            }
            for (const auto &filterProperty : remainingFilters) {
                //TODO implement other comparison operators than equality
                if (domainObject->getProperty(filterProperty) != query.propertyFilter.value(filterProperty)) {
                    return false;
                }
            }
            return true;
        };

        return filteredSet(resultSet, filter, storage, adaptorFactory, baseRevision, topRevision, resourceInstanceIdentifier);
    }

public:

    void read(const Akonadi2::Query &query, const QPair<qint64, qint64> &revisionRange, const QSharedPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > &resultProvider)
    {
        auto storage = QSharedPointer<Akonadi2::Storage>::create(Akonadi2::Store::storageLocation(), mResourceInstanceIdentifier);
        storage->setDefaultErrorHandler([](const Akonadi2::Storage::Error &error) {
            Warning() << "Error during query: " << error.store << error.message;
        });

        storage->startTransaction(Akonadi2::Storage::ReadOnly);
        //TODO start transaction on indexes as well

        Log() << "Querying" << revisionRange.first << revisionRange.second;
        auto resultSet = getResultSet(query, storage, mDomainTypeAdaptorFactory, mResourceInstanceIdentifier, revisionRange.first, revisionRange.second);
        auto resultCallback = std::bind(&Akonadi2::ResultProvider<typename DomainType::Ptr>::add, resultProvider, std::placeholders::_1);
        while(resultSet.next([resultCallback](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &value) -> bool {
            resultCallback(Akonadi2::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<DomainType>(*value));
            return true;
        })){};
        //TODO replay removals and modifications
        storage->abortTransaction();
    }

private:
    DomainTypeAdaptorFactoryInterface::Ptr mDomainTypeAdaptorFactory;
    QByteArray mResourceInstanceIdentifier;
};
