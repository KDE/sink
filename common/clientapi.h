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
#include <QSharedPointer>

#include <Async/Async>

#include "query.h"
#include "applicationdomaintype.h"

class QAbstractItemModel;

namespace Akonadi2 {

/**
 * Store interface used in the client API.
 */
class Store {
private:
    static QList<QByteArray> getResources(const QList<QByteArray> &resourceFilter, const QByteArray &type);

public:
    static QString storageLocation();
    static QByteArray resourceName(const QByteArray &instanceIdentifier);

    enum Roles {
        DomainObjectRole = Qt::UserRole + 1, //Must be the same as in ModelResult
        ChildrenFetchedRole,
        DomainObjectBaseRole
    };

    /**
     * Asynchronusly load a dataset with tree structure information
     */
    template <class DomainType>
    static QSharedPointer<QAbstractItemModel> loadModel(Query query);

    /**
     * Create a new entity.
     */
    template <class DomainType>
    static KAsync::Job<void> create(const DomainType &domainObject);

    /**
     * Modify an entity.
     * 
     * This includes moving etc. since these are also simple settings on a property.
     */
    template <class DomainType>
    static KAsync::Job<void> modify(const DomainType &domainObject);

    /**
     * Remove an entity.
     */
    template <class DomainType>
    static KAsync::Job<void> remove(const DomainType &domainObject);

    /**
     * Shutdown resource.
     */
    static KAsync::Job<void> shutdown(const QByteArray &resourceIdentifier);

    /**
     * Start resource.
     * 
     * The resource is ready for operation once this command completes.
     * This command is only necessary if a resource was shutdown previously,
     * otherwise the resource process will automatically start as necessary.
     */
    static KAsync::Job<void> start(const QByteArray &resourceIdentifier);

    /**
     * Synchronize data to local cache.
     */
    static KAsync::Job<void> synchronize(const Akonadi2::Query &query);

    /**
     * Removes a resource from disk.
     */
    static void removeFromDisk(const QByteArray &resourceIdentifier);
};


}

