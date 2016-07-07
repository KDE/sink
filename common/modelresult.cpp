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

#include "domain/folder.h"
#include "log.h"

SINK_DEBUG_AREA("modelresult")

static uint qHash(const Sink::ApplicationDomain::ApplicationDomainType &type)
{
    // Q_ASSERT(!type.resourceInstanceIdentifier().isEmpty());
    Q_ASSERT(!type.identifier().isEmpty());
    return qHash(type.resourceInstanceIdentifier() + type.identifier());
}

template <class T, class Ptr>
ModelResult<T, Ptr>::ModelResult(const Sink::Query &query, const QList<QByteArray> &propertyColumns)
    : QAbstractItemModel(), mPropertyColumns(propertyColumns), mQuery(query)
{
}

static qint64 getIdentifier(const QModelIndex &idx)
{
    if (!idx.isValid()) {
        return 0;
    }
    return idx.internalId();
}

template <class T, class Ptr>
qint64 ModelResult<T, Ptr>::parentId(const Ptr &value)
{
    if (!mQuery.parentProperty.isEmpty()) {
        const auto identifier = value->getProperty(mQuery.parentProperty).toByteArray();
        if (!identifier.isEmpty()) {
            return qHash(T(value->resourceInstanceIdentifier(), identifier, 0, QSharedPointer<Sink::ApplicationDomain::BufferAdaptor>()));
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
    SinkWarning() << "Index not available " << row << column << parent;
    Q_ASSERT(false);
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
    if (mQuery.parentProperty.isEmpty() && parent.isValid()) {
        return false;
    }
    //Figure out whether we have children
    if (canFetchMore(parent)) {
        //Since we don't retrieve that information as part of the entity,
        //we have to query for the children to see if we have some
        auto p = const_cast<ModelResult<T, Ptr>*>(this);
        p->fetchMore(parent);
    }
    return QAbstractItemModel::hasChildren(parent);
}

template <class T, class Ptr>
bool ModelResult<T, Ptr>::canFetchMore(const QModelIndex &parent) const
{
    const auto id = parent.internalId();
    return !mEntityChildrenFetched.contains(id) || mEntityChildrenFetchComplete.contains(id);
}

template <class T, class Ptr>
void ModelResult<T, Ptr>::fetchMore(const QModelIndex &parent)
{
    SinkTrace() << "Fetching more: " << parent;
    fetchEntities(parent);
}

template <class T, class Ptr>
void ModelResult<T, Ptr>::add(const Ptr &value)
{
    const auto childId = qHash(*value);
    const auto id = parentId(value);
    // Ignore updates we get before the initial fetch is done
    if (!mEntityChildrenFetched.contains(id)) {
        SinkTrace() << "Too early" << id;
        return;
    }
    auto parent = createIndexFromId(id);
    // qDebug() << "Added entity " << childId << value->identifier() << id;
    const auto keys = mTree[id];
    int index = 0;
    for (; index < keys.size(); index++) {
        if (childId < keys.at(index)) {
            break;
        }
    }
    if (mEntities.contains(childId)) {
        SinkWarning() << "Entity already in model " << value->identifier();
        return;
    }
    // qDebug() << "Inserting rows " << index << parent;
    beginInsertRows(parent, index, index);
    mEntities.insert(childId, value);
    mTree[id].insert(index, childId);
    mParents.insert(childId, id);
    endInsertRows();
    // qDebug() << "Inserted rows " << mTree[id].size();
}


template <class T, class Ptr>
void ModelResult<T, Ptr>::remove(const Ptr &value)
{
    auto childId = qHash(*value);
    auto id = parentId(value);
    auto parent = createIndexFromId(id);
    // qDebug() << "Removed entity" << childId;
    auto index = mTree[id].indexOf(childId);
    beginRemoveRows(parent, index, index);
    mEntities.remove(childId);
    mTree[id].removeAll(childId);
    mParents.remove(childId);
    // TODO remove children
    endRemoveRows();
}

template <class T, class Ptr>
void ModelResult<T, Ptr>::fetchEntities(const QModelIndex &parent)
{
    Q_ASSERT(QThread::currentThread() == this->thread());
    const auto id = getIdentifier(parent);
    mEntityChildrenFetchComplete.remove(id);
    mEntityChildrenFetched.insert(id);
    SinkTrace() << "Loading child entities of parent " << id;
    if (loadEntities) {
        loadEntities(parent.data(DomainObjectRole).template value<Ptr>());
    } else {
        SinkWarning() << "No way to fetch entities";
    }
}

template <class T, class Ptr>
void ModelResult<T, Ptr>::setFetcher(const std::function<void(const Ptr &parent)> &fetcher)
{
    SinkTrace() << "Setting fetcher";
    loadEntities = fetcher;
}

template <class T, class Ptr>
void ModelResult<T, Ptr>::setEmitter(const typename Sink::ResultEmitter<Ptr>::Ptr &emitter)
{
    setFetcher([this](const Ptr &parent) { mEmitter->fetch(parent); });

    emitter->onAdded([this](const Ptr &value) {
        threadBoundary.callInMainThread([this, value]() {
            add(value);
        });
    });
    emitter->onModified([this](const Ptr &value) {
        threadBoundary.callInMainThread([this, value]() {
            modify(value);
        });
    });
    emitter->onRemoved([this](const Ptr &value) {
        threadBoundary.callInMainThread([this, value]() {
            remove(value);
        });
    });
    emitter->onInitialResultSetComplete([this](const Ptr &parent) {
        SinkTrace() << "Initial result set complete";
        const qint64 parentId = parent ? qHash(*parent) : 0;
        const auto parentIndex = createIndexFromId(parentId);
        mEntityChildrenFetchComplete.insert(parentId);
        emit dataChanged(parentIndex, parentIndex, QVector<int>() << ChildrenFetchedRole);
    });
    mEmitter = emitter;
}

template <class T, class Ptr>
bool ModelResult<T, Ptr>::childrenFetched(const QModelIndex &index) const
{
    return mEntityChildrenFetchComplete.contains(getIdentifier(index));
}

template <class T, class Ptr>
void ModelResult<T, Ptr>::modify(const Ptr &value)
{
    auto childId = qHash(*value);
    auto id = parentId(value);
    // Ignore updates we get before the initial fetch is done
    if (!mEntityChildrenFetched.contains(id)) {
        return;
    }
    auto parent = createIndexFromId(id);
    // qDebug() << "Modified entity" << childId;
    auto i = mTree[id].indexOf(childId);
    mEntities.remove(childId);
    mEntities.insert(childId, value);
    // TODO check for change of parents
    auto idx = index(i, 0, parent);
    emit dataChanged(idx, idx);
}

template class ModelResult<Sink::ApplicationDomain::Folder, Sink::ApplicationDomain::Folder::Ptr>;
template class ModelResult<Sink::ApplicationDomain::Mail, Sink::ApplicationDomain::Mail::Ptr>;
template class ModelResult<Sink::ApplicationDomain::Event, Sink::ApplicationDomain::Event::Ptr>;
template class ModelResult<Sink::ApplicationDomain::SinkResource, Sink::ApplicationDomain::SinkResource::Ptr>;
template class ModelResult<Sink::ApplicationDomain::SinkAccount, Sink::ApplicationDomain::SinkAccount::Ptr>;
template class ModelResult<Sink::ApplicationDomain::Identity, Sink::ApplicationDomain::Identity::Ptr>;
