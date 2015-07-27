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

#include "entitystorage.h"

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

void EntityStorageBase::readValue(const QSharedPointer<Akonadi2::Storage> &storage, const QByteArray &key, const std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &)> &resultCallback)
{
    scan(storage, key, [=](const QByteArray &key, const Akonadi2::Entity &entity) {
        const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(entity.metadata());
        qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
        //This only works for a 1:1 mapping of resource to domain types.
        //Not i.e. for tags that are stored as flags in each entity of an imap store.
        //additional properties that don't have a 1:1 mapping (such as separately stored tags),
        //could be added to the adaptor
        auto domainObject = create(key, revision, mDomainTypeAdaptorFactory->createAdaptor(entity));
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

ResultSet EntityStorageBase::filteredSet(const ResultSet &resultSet, const std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter, const QSharedPointer<Akonadi2::Storage> &storage, qint64 baseRevision, qint64 topRevision)
{
    auto resultSetPtr = QSharedPointer<ResultSet>::create(resultSet);

    //Read through the source values and return whatever matches the filter
    std::function<bool(std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &)>)> generator = [this, resultSetPtr, storage, filter](std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &)> callback) -> bool {
        while (resultSetPtr->next()) {
            readValue(storage, resultSetPtr->id(), [this, filter, callback](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject) {
                if (filter(domainObject)) {
                    callback(domainObject);
                }
            });
        }
        return false;
    };
    return ResultSet(generator);
}

ResultSet EntityStorageBase::getResultSet(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::Storage> &storage, qint64 baseRevision, qint64 topRevision)
{
    QSet<QByteArray> appliedFilters;
    ResultSet resultSet = queryIndexes(query, mResourceInstanceIdentifier, appliedFilters);
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

    return filteredSet(resultSet, filter, storage, baseRevision, topRevision);
}
