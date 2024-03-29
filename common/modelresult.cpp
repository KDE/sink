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
#include "modelresult.h"

#include <QDebug>
#include <QThread>
#include <QPointer>

#include "log.h"
#include "notifier.h"
#include "notification.h"

using namespace Sink;

static uint getInternalIdentifer(const QByteArray &resourceId, const QByteArray &entityId)
{
    return qHash(resourceId + entityId);
}

static uint qHash(const Sink::ApplicationDomain::ApplicationDomainType &type)
{
    Q_ASSERT(!type.identifier().isEmpty());
    return getInternalIdentifer(type.resourceInstanceIdentifier(), type.identifier());
}

static qint64 getIdentifier(const QModelIndex &idx)
{
    if (!idx.isValid()) {
        return 0;
    }
    return idx.internalId();
}

template <class T, class Ptr>
ModelResult<T, Ptr>::ModelResult(const Sink::Query &query, const QList<QByteArray> &propertyColumns, const Sink::Log::Context &ctx)
    : QAbstractItemModel(), mLogCtx(ctx.subContext("modelresult")), mPropertyColumns(propertyColumns), mQuery(query)
{
    if (query.flags().testFlag(Sink::Query::UpdateStatus)) {
        Sink::Query resourceQuery;
        resourceQuery.setFilter(query.getResourceFilter());
        mNotifier.reset(new Sink::Notifier{resourceQuery});
        mNotifier->registerHandler([this](const Notification &notification) {
            switch (notification.type) {
                case Notification::Status:
                case Notification::Warning:
                case Notification::Error:
                case Notification::Info:
                case Notification::Progress:
                    //These are the notifications we care about
                    break;
                default:
                    //We're not interested
                    return;
            }
            if (notification.resource.isEmpty() || notification.entities.isEmpty()) {
                return;
            }

            QVector<qint64> idList;
            for (const auto &entity : notification.entities) {
                auto id = getInternalIdentifer(notification.resource, entity);
                if (mEntities.contains(id)) {
                    idList << id;
                }
            }

            if (idList.isEmpty()) {
                //We don't have this entity in our model
                return;
            }
            const int newStatus = [&] {
                if (notification.type == Notification::Warning || notification.type == Notification::Error) {
                    return ApplicationDomain::SyncStatus::SyncError;
                }
                if (notification.type == Notification::Info) {
                    switch (notification.code) {
                        case ApplicationDomain::SyncInProgress:
                            return ApplicationDomain::SyncInProgress;
                        case ApplicationDomain::SyncSuccess:
                            return ApplicationDomain::SyncSuccess;
                        case ApplicationDomain::SyncError:
                            return ApplicationDomain::SyncError;
                        case ApplicationDomain::NoSyncStatus:
                            break;
                    }
                    return ApplicationDomain::NoSyncStatus;
                }
                if (notification.type == Notification::Progress) {
                    return ApplicationDomain::SyncStatus::SyncInProgress;
                }
                return ApplicationDomain::NoSyncStatus;
            }();

            for (const auto id : idList) {
                const auto oldStatus = mEntityStatus.value(id);
                QVector<int> changedRoles;
                if (oldStatus != newStatus) {
                    SinkTraceCtx(mLogCtx) << "Status changed for entity:" << newStatus << ", id: " << id;
                    mEntityStatus.insert(id, newStatus);
                    changedRoles << StatusRole;
                }

                if (notification.type == Notification::Progress) {
                    changedRoles << ProgressRole;
                } else if (notification.type == Notification::Warning || notification.type == Notification::Error) {
                    changedRoles << WarningRole;
                }

                if (!changedRoles.isEmpty()) {
                    const auto idx = createIndexFromId(id);
                    SinkTraceCtx(mLogCtx) << "Index changed:" << idx << changedRoles;
                    //We don't emit the changedRoles because the consuming model likely remaps the role anyways and would then need to translate dataChanged signals as well.
                    emit dataChanged(idx, idx);
                }
            }
        });
    }
}

template <class T, class Ptr>
ModelResult<T, Ptr>::~ModelResult()
{
    if (mEmitter) {
        mEmitter->waitForMethodExecutionEnd();
    }
}

template <class T, class Ptr>
qint64 ModelResult<T, Ptr>::parentId(const Ptr &value)
{
    if (!mQuery.parentProperty().isEmpty()) {
        const auto identifier = value->getProperty(mQuery.parentProperty()).toByteArray();
        if (!identifier.isEmpty()) {
            return getInternalIdentifer(value->resourceInstanceIdentifier(), identifier);
        }
    }
    return 0;
}

template <class T, class Ptr>
int ModelResult<T, Ptr>::rowCount(const QModelIndex &parent) const
{
    Q_ASSERT(QThread::currentThread() == this->thread());
    return mTree[getIdentifier(parent)].size();
}

template <class T, class Ptr>
int ModelResult<T, Ptr>::columnCount(const QModelIndex &parent) const
{
    Q_ASSERT(QThread::currentThread() == this->thread());
    return mPropertyColumns.size();
}

template <class T, class Ptr>
QVariant ModelResult<T, Ptr>::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (section < mPropertyColumns.size()) {
            return mPropertyColumns.at(section);
        }
    }
    return QVariant();
}

template <class T, class Ptr>
QVariant ModelResult<T, Ptr>::data(const QModelIndex &index, int role) const
{
    Q_ASSERT(QThread::currentThread() == this->thread());
    if (role == DomainObjectRole && index.isValid()) {
        Q_ASSERT(mEntities.contains(index.internalId()));
        return QVariant::fromValue(mEntities.value(index.internalId()));
    }
    if (role == DomainObjectBaseRole && index.isValid()) {
        Q_ASSERT(mEntities.contains(index.internalId()));
        return QVariant::fromValue(mEntities.value(index.internalId()).template staticCast<Sink::ApplicationDomain::ApplicationDomainType>());
    }
    if (role == ChildrenFetchedRole) {
        return childrenFetched(index);
    }
    if (role == StatusRole) {
        auto it = mEntityStatus.constFind(index.internalId());
        if (it != mEntityStatus.constEnd()) {
            return *it;
        }
        return {};
    }
    if (role == Qt::DisplayRole && index.isValid()) {
        if (index.column() < mPropertyColumns.size()) {
            Q_ASSERT(mEntities.contains(index.internalId()));
            auto entity = mEntities.value(index.internalId());
            return entity->getProperty(mPropertyColumns.at(index.column())).toString();
        } else {
            return "No data available";
        }
    }
    return QVariant();
}

template <class T, class Ptr>
QModelIndex ModelResult<T, Ptr>::index(int row, int column, const QModelIndex &parent) const
{
    Q_ASSERT(QThread::currentThread() == this->thread());
    const auto id = getIdentifier(parent);
    const auto list = mTree.value(id);
    if (list.size() > row) {
        const auto childId = list.at(row);
        return createIndex(row, column, childId);
    }
    SinkWarningCtx(mLogCtx) << "Index not available " << row << column << parent;
    return QModelIndex();
}

template <class T, class Ptr>
QModelIndex ModelResult<T, Ptr>::createIndexFromId(const qint64 &id) const
{
    Q_ASSERT(QThread::currentThread() == this->thread());
    if (id == 0) {
        return QModelIndex();
    }
    auto grandParentId = mParents.value(id, 0);
    auto row = mTree.value(grandParentId).indexOf(id);
    Q_ASSERT(row >= 0);
    return createIndex(row, 0, id);
}

template <class T, class Ptr>
QModelIndex ModelResult<T, Ptr>::parent(const QModelIndex &index) const
{
    auto id = getIdentifier(index);
    auto parentId = mParents.value(id);
    return createIndexFromId(parentId);
}

template <class T, class Ptr>
bool ModelResult<T, Ptr>::hasChildren(const QModelIndex &parent) const
{
    if (mQuery.parentProperty().isEmpty() && parent.isValid()) {
        return false;
    }
    return QAbstractItemModel::hasChildren(parent);
}

template <class T, class Ptr>
bool ModelResult<T, Ptr>::canFetchMore(const QModelIndex &parent) const
{
    //We fetch trees immediately so can never fetch more
    if (parent.isValid() || mFetchedAll) {
        return false;
    }
    return true;
}

template <class T, class Ptr>
void ModelResult<T, Ptr>::fetchMore(const QModelIndex &parent)
{
    SinkTraceCtx(mLogCtx) << "Fetching more: " << parent;
    Q_ASSERT(QThread::currentThread() == this->thread());
    //We only suppor fetchMore for flat lists
    if (parent.isValid()) {
        return;
    }
    //There is already a fetch in progress, don't fetch again.
    if (mFetchInProgress) {
        SinkTraceCtx(mLogCtx) << "A fetch is already in progress.";
        return;
    }
    mFetchInProgress = true;
    mFetchComplete = false;
    SinkTraceCtx(mLogCtx) << "Fetching more.";
    if (loadEntities) {
        loadEntities();
    } else {
        SinkWarningCtx(mLogCtx) << "No way to fetch entities";
    }
}

template <class T, class Ptr>
bool ModelResult<T, Ptr>::allParentsAvailable(qint64 id) const
{
    auto p = id;
    while (p) {
        if (!mEntities.contains(p)) {
            return false;
        }
        p = mParents.value(p, 0);
    }
    return true;
}

template <class T, class Ptr>
void ModelResult<T, Ptr>::add(const Ptr &value)
{
    const auto childId = qHash(*value);
    const auto pId = parentId(value);
    if (mEntities.contains(childId)) {

        mEntitiesToRemove.remove(childId);
        return;
    }
    const auto keys = mTree[pId];
    int idx = 0;
    for (; idx < keys.size(); idx++) {
        if (childId < keys.at(idx)) {
            break;
        }
    }
    bool parentIsVisible = allParentsAvailable(pId);
    // SinkTraceCtx(mLogCtx) << "Inserting rows " << index << parent;
    if (parentIsVisible) {
        auto parent = createIndexFromId(pId);
        beginInsertRows(parent, idx, idx);
    }
    mEntities.insert(childId, value);
    mTree[pId].insert(idx, childId);
    mParents.insert(childId, pId);
    if (parentIsVisible) {
        endInsertRows();
    }
    // SinkTraceCtx(mLogCtx) << "Inserted rows " << mTree[id].size();
}


template <class T, class Ptr>
void ModelResult<T, Ptr>::remove(const Ptr &value)
{
    auto childId = qHash(*value);
    if (!mEntities.contains(childId)) {
        return;
    }
    //The removed entity will have no properties, but we at least need the parent property.
    auto actualEntity = mEntities.value(childId);
    auto id = parentId(actualEntity);
    auto parent = createIndexFromId(id);
    SinkTraceCtx(mLogCtx) << "Removed entity" << childId;
    auto index = mTree[id].indexOf(childId);
    if (index >= 0) {
        beginRemoveRows(parent, index, index);
        mEntities.remove(childId);
        mTree[id].removeAll(childId);
        mParents.remove(childId);
        // TODO remove children
        endRemoveRows();
    }
}

template <class T, class Ptr>
void ModelResult<T, Ptr>::setFetcher(const std::function<void()> &fetcher)
{
    SinkTraceCtx(mLogCtx) << "Setting fetcher";
    loadEntities = fetcher;
}

template <class T, class Ptr>
void ModelResult<T, Ptr>::updateQuery(const Sink::Query &query)
{
    SinkTraceCtx(mLogCtx) << "Triggering query update";
    mQuery = query;
    mPropertyColumns = query.requestedProperties;
    mEntitiesToRemove = mEntities.keys().toSet();
    mFetchComplete = false;
    mFetchInProgress = false;
    fetchMore({});
}

template <class T, class Ptr>
void ModelResult<T, Ptr>::setEmitter(const typename Sink::ResultEmitter<Ptr>::Ptr &emitter)
{
    if (mEmitter) {
        mEmitter->waitForMethodExecutionEnd();
        mEmitter.clear();
    }

    setFetcher([this]() { mEmitter->fetch(); });

    QPointer<QObject> guard(this);
    emitter->onAdded([this, guard](const Ptr &value) {
        SinkTraceCtx(mLogCtx) << "Received addition: " << value->identifier();
        Q_ASSERT(guard);
        threadBoundary.callInMainThread([this, value, guard]() {
            Q_ASSERT(guard);
            add(value);
        });
    });
    emitter->onModified([this, guard](const Ptr &value) {
        SinkTraceCtx(mLogCtx) << "Received modification: " << value->identifier();
        Q_ASSERT(guard);
        threadBoundary.callInMainThread([this, value]() {
            modify(value);
        });
    });
    emitter->onRemoved([this, guard](const Ptr &value) {
        SinkTraceCtx(mLogCtx) << "Received removal: " << value->identifier();
        Q_ASSERT(guard);
        threadBoundary.callInMainThread([this, value]() {
            remove(value);
        });
    });
    emitter->onInitialResultSetComplete([this, guard](bool fetchedAll) {
        SinkTraceCtx(mLogCtx) << "Initial result set complete. Fetched all: " << fetchedAll;
        Q_ASSERT(guard);
        Q_ASSERT(QThread::currentThread() == this->thread());
        mFetchInProgress = false;
        mFetchedAll = fetchedAll;
        mFetchComplete = true;

        for (const auto &entityId : mEntitiesToRemove) {
            remove(mEntities.value(entityId));
        }
        mEntitiesToRemove.clear();

        emit dataChanged({}, {}, QVector<int>() << ChildrenFetchedRole);
    });
    mEmitter = emitter;
}

template <class T, class Ptr>
bool ModelResult<T, Ptr>::childrenFetched(const QModelIndex &index) const
{
    return mFetchComplete;
}

template <class T, class Ptr>
void ModelResult<T, Ptr>::modify(const Ptr &value)
{
    auto childId = qHash(*value);
    if (!mEntities.contains(childId)) {
        //Happens because the DatabaseQuery emits modifiations also if the item used to be filtered.
        SinkTraceCtx(mLogCtx) << "Tried to modify a value that is not yet part of the model";
        add(value);
        return;
    }
    auto id = parentId(value);
    auto parent = createIndexFromId(id);
    SinkTraceCtx(mLogCtx) << "Modified entity:" << value->identifier() << ", id: " << childId;
    auto i = mTree[id].indexOf(childId);
    Q_ASSERT(i >= 0);
    mEntities.remove(childId);
    mEntities.insert(childId, value);
    // TODO check for change of parents
    auto idx = index(i, 0, parent);
    emit dataChanged(idx, idx);
}

#define REGISTER_TYPE(T) \
    template class ModelResult<T, T::Ptr>; \

SINK_REGISTER_TYPES()
