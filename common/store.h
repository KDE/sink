/*
 * Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "sink_export.h"
#include <QString>
#include <QSharedPointer>

#include <KAsync/Async>

#include "query.h"
#include "applicationdomaintype.h"

class QAbstractItemModel;

namespace Sink {

/**
 * The unified Sink Store.
 *
 * This is the primary interface for clients to interact with Sink.
 * It provides a unified store where all data provided by various resources can be accessed and modified.
 */
namespace Store {

QString SINK_EXPORT storageLocation();

// Must be the same as in ModelResult
enum Roles
{
    DomainObjectRole = Qt::UserRole + 1,
    ChildrenFetchedRole,
    DomainObjectBaseRole,
    StatusRole, //ApplicationDomain::SyncStatus
    WarningRole, //ApplicationDomain::Warning, only if status == warning || status == error
    ProgressRole //ApplicationDomain::Progress
};

/**
* Asynchronusly load a dataset with tree structure information
*/
template <class DomainType>
QSharedPointer<QAbstractItemModel> SINK_EXPORT loadModel(const Query &query);

template <class DomainType>
void SINK_EXPORT updateModel(const Query &query, const QSharedPointer<QAbstractItemModel> &model);

/**
 * Create a new entity.
 */
template <class DomainType>
KAsync::Job<void> SINK_EXPORT create(const DomainType &domainObject);

/**
 * Modify an entity.
 *
 * This includes moving etc. since these are also simple settings on a property.
 * Note that the modification will be dropped if there is no changedProperty on the domain object.
 */
template <class DomainType>
KAsync::Job<void> SINK_EXPORT modify(const DomainType &domainObject);

/**
 * Modify a set of entities identified by @param query.
 * 
 * Note that the modification will be dropped if there is no changedProperty on the domain object.
 */
template <class DomainType>
KAsync::Job<void> SINK_EXPORT modify(const Query &query, const DomainType &domainObject);

/**
 * Remove an entity.
 */
template <class DomainType>
KAsync::Job<void> SINK_EXPORT remove(const DomainType &domainObject);

/**
 * Remove a set of entities identified by @param query.
 */
template <class DomainType>
KAsync::Job<void> SINK_EXPORT remove(const Query &query);

/**
 * Move an entity to a new resource.
 */
template <class DomainType>
KAsync::Job<void> SINK_EXPORT move(const DomainType &domainObject, const QByteArray &newResource);

/**
 * Copy an entity to a new resource.
 */
template <class DomainType>
KAsync::Job<void> SINK_EXPORT copy(const DomainType &domainObject, const QByteArray &newResource);

/**
 * Synchronize data to local cache.
 */
KAsync::Job<void> SINK_EXPORT synchronize(const Sink::Query &query);
KAsync::Job<void> SINK_EXPORT synchronize(const Sink::SyncScope &query);

/**
 * Abort all running synchronization commands.
 */
KAsync::Job<void> SINK_EXPORT abortSynchronization(const Sink::SyncScope &scope);

/**
 * Removes all resource data from disk.
 *
 * This will not touch the configuration. All commands that that arrived at the resource before this command will be dropped. All commands that arrived later will be executed.
 */
KAsync::Job<void> SINK_EXPORT removeDataFromDisk(const QByteArray &resourceIdentifier);

struct UpgradeResult {
    bool upgradeExecuted;
};

/**
 * Run upgrade jobs.
 *
 * Run this to upgrade your local database to a new version.
 * Note that this may:
 * * take a while
 * * remove some/all of your local caches
 *
 * Note: The initial implementation simply calls removeDataFromDisk for all resources.
 */
KAsync::Job<UpgradeResult> SINK_EXPORT upgrade();

template <class DomainType>
KAsync::Job<DomainType> SINK_EXPORT fetchOne(const Sink::Query &query);

template <class DomainType>
KAsync::Job<QList<typename DomainType::Ptr>> SINK_EXPORT fetchAll(const Sink::Query &query);

template <class DomainType>
KAsync::Job<QList<typename DomainType::Ptr>> SINK_EXPORT fetch(const Sink::Query &query, int minimumAmount = 0);

template <class DomainType>
DomainType SINK_EXPORT readOne(const Sink::Query &query);

template <class DomainType>
QList<DomainType> SINK_EXPORT read(const Sink::Query &query);
}
}
