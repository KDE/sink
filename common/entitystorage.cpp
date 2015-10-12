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

static void scan(const Akonadi2::Storage::Transaction &transaction, const QByteArray &key, std::function<bool(const QByteArray &key, const Akonadi2::Entity &entity)> callback, const QByteArray &bufferType)
{

    transaction.openDatabase(bufferType + ".main").findLatest(key, [=](const QByteArray &key, const QByteArray &value) -> bool {
        //Extract buffers
        Akonadi2::EntityBuffer buffer(value.data(), value.size());

        //FIXME implement buffer.isValid()
        // const auto resourceBuffer = Akonadi2::EntityBuffer::readBuffer<DummyEvent>(buffer.entity().resource());
        // const auto localBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::ApplicationDomain::Buffer::Event>(buffer.entity().local());
        // const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(buffer.entity().metadata());

        // if ((!resourceBuffer && !localBuffer) || !metadataBuffer) {
        //     qWarning() << "invalid buffer " << QByteArray::fromRawData(static_cast<char*>(keyValue), keySize);
        //     return true;
        // }
        //
        //We're cutting the revision off the key
        return callback(Akonadi2::Storage::uidFromKey(key), buffer.entity());
    },
    [](const Akonadi2::Storage::Error &error) {
        qWarning() << "Error during query: " << error.message;
    });
}

void EntityStorageBase::readEntity(const Akonadi2::Storage::Transaction &transaction, const QByteArray &key, const std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &, Akonadi2::Operation)> &resultCallback)
{
    //This only works for a 1:1 mapping of resource to domain types.
    //Not i.e. for tags that are stored as flags in each entity of an imap store.
    //additional properties that don't have a 1:1 mapping (such as separately stored tags),
    //could be added to the adaptor.
    //TODO: resource implementations should be able to customize the retrieval function for non 1:1 entity-buffer mapping cases
    scan(transaction, key, [=](const QByteArray &key, const Akonadi2::Entity &entity) {
        const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(entity.metadata());
        Q_ASSERT(metadataBuffer);
        qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
        auto operation = metadataBuffer->operation();

        auto domainObject = create(key, revision, mDomainTypeAdaptorFactory->createAdaptor(entity));
        if (operation == Akonadi2::Operation_Removal) {
            resultCallback(create(key, revision, mDomainTypeAdaptorFactory->createAdaptor(entity)), operation);
        } else {
            resultCallback(create(key, revision, mDomainTypeAdaptorFactory->createAdaptor(entity)), operation);
        }
        return false;
    }, mBufferType);
}

static ResultSet fullScan(const Akonadi2::Storage::Transaction &transaction, const QByteArray &bufferType)
{
    //TODO use a result set with an iterator, to read values on demand
    QVector<QByteArray> keys;
    transaction.openDatabase(bufferType + ".main").scan(QByteArray(), [&](const QByteArray &key, const QByteArray &value) -> bool {
        //Skip internals
        if (Akonadi2::Storage::isInternalKey(key)) {
            return true;
        }
        keys << Akonadi2::Storage::uidFromKey(key);
        return true;
    },
    [](const Akonadi2::Storage::Error &error) {
        qWarning() << "Error during query: " << error.message;
    });

    Trace() << "Full scan found " << keys.size() << " results";
    return ResultSet(keys);
}

ResultSet EntityStorageBase::filteredSet(const ResultSet &resultSet, const std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter, const Akonadi2::Storage::Transaction &transaction, bool initialQuery)
{
    auto resultSetPtr = QSharedPointer<ResultSet>::create(resultSet);

    //Read through the source values and return whatever matches the filter
    std::function<bool(std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &, Akonadi2::Operation)>)> generator = [this, resultSetPtr, &transaction, filter, initialQuery](std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &, Akonadi2::Operation)> callback) -> bool {
        while (resultSetPtr->next()) {
            readEntity(transaction, resultSetPtr->id(), [this, filter, callback, initialQuery](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject, Akonadi2::Operation operation) {
                if (filter(domainObject)) {
                    if (initialQuery) {
                        //We're not interested in removals during the initial query
                        if (operation != Akonadi2::Operation_Removal) {
                            callback(domainObject, Akonadi2::Operation_Creation);
                        }
                    } else {
                        callback(domainObject, operation);
                    }
                }
            });
        }
        return false;
    };
    return ResultSet(generator);
}

ResultSet EntityStorageBase::loadInitialResultSet(const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters)
{
    QSet<QByteArray> appliedFilters;
    auto resultSet = queryIndexes(query, mResourceInstanceIdentifier, appliedFilters, transaction);
    remainingFilters = query.propertyFilter.keys().toSet() - appliedFilters;

    //We do a full scan if there were no indexes available to create the initial set.
    if (appliedFilters.isEmpty()) {
        //TODO this should be replaced by an index lookup as well
        return fullScan(transaction, mBufferType);
    }
    return resultSet;
}

ResultSet EntityStorageBase::getResultSet(const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, qint64 baseRevision)
{
    const qint64 topRevision = Akonadi2::Storage::maxRevision(transaction);
    QSet<QByteArray> remainingFilters = query.propertyFilter.keys().toSet();
    ResultSet resultSet;
    const bool initialQuery = (baseRevision == 1);
    if (initialQuery) {
        Trace() << "Initial result set update";
        resultSet = loadInitialResultSet(query, transaction, remainingFilters);
    } else {
        //TODO fallback in case the old revision is no longer available to clear + redo complete initial scan
        Trace() << "Incremental result set update" << baseRevision << topRevision;
        auto revisionCounter = QSharedPointer<qint64>::create(baseRevision);
        resultSet = ResultSet([revisionCounter, topRevision, &transaction, this]() -> QByteArray {
            //Spit out the revision keys one by one.
            while (*revisionCounter <= topRevision) {
                const auto uid = Akonadi2::Storage::getUidFromRevision(transaction, *revisionCounter);
                const auto type = Akonadi2::Storage::getTypeFromRevision(transaction, *revisionCounter);
                Trace() << "Revision" << *revisionCounter << type << uid;
                if (type != mBufferType) {
                    //Skip revision
                    *revisionCounter += 1;
                    continue;
                }
                const auto key = Akonadi2::Storage::assembleKey(uid, *revisionCounter);
                *revisionCounter += 1;
                return key;
            }
            //We're done
            return QByteArray();
        });
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

    return filteredSet(resultSet, filter, transaction, initialQuery);
}
