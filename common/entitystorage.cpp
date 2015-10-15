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


ResultSet EntityStorageBase::getResultSet(const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, qint64 baseRevision)
{
    QSet<QByteArray> remainingFilters = query.propertyFilter.keys().toSet();
    ResultSet resultSet;
    const bool initialQuery = (baseRevision == 1);
    if (initialQuery) {
        Trace() << "Initial result set update";
        resultSet = loadInitialResultSet(query, transaction, remainingFilters);
    } else {
        //TODO fallback in case the old revision is no longer available to clear + redo complete initial scan
        Trace() << "Incremental result set update" << baseRevision;
        resultSet = loadIncrementalResultSet(baseRevision, query, transaction, remainingFilters);
    }

    auto filter = [remainingFilters, query, baseRevision](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject) -> bool {
        // if (topRevision > 0) {
        //     Trace() << "filtering by revision " << domainObject->revision();
        //     if (domainObject->revision() < baseRevision) {
        //         return false;
        //     }
        // }
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
