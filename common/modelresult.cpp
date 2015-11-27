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

#include "domain/folder.h"
#include "log.h"

template<class T, class Ptr>
ModelResult<T, Ptr>::ModelResult(const Akonadi2::Query &query, const QList<QByteArray> &propertyColumns)
    :QAbstractItemModel(),
    mPropertyColumns(propertyColumns),
    mQuery(query)
{
}

static qint64 getIdentifier(const QModelIndex &idx)
{
    if (!idx.isValid()) {
        return 0;
    }
    return idx.internalId();
}

template<class T, class Ptr>
qint64 ModelResult<T, Ptr>::parentId(const Ptr &value)
{
    if (!mQuery.parentProperty.isEmpty()) {
        return qHash(value->getProperty(mQuery.parentProperty).toByteArray());
    }
    return qHash(QByteArray());
}

template<class T, class Ptr>
int ModelResult<T, Ptr>::rowCount(const QModelIndex &parent) const
{
    return mTree[getIdentifier(parent)].size();
}

template<class T, class Ptr>
int ModelResult<T, Ptr>::columnCount(const QModelIndex &parent) const
{
    return mPropertyColumns.size();
}

template<class T, class Ptr>
QVariant ModelResult<T, Ptr>::data(const QModelIndex &index, int role) const
{
    if (role == DomainObjectRole) {
        Q_ASSERT(mEntities.contains(index.internalId()));
        return QVariant::fromValue(mEntities.value(index.internalId()));
    }
    if (role == Qt::DisplayRole) {
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

template<class T, class Ptr>
QModelIndex ModelResult<T, Ptr>::index(int row, int column, const QModelIndex &parent) const
{
    auto id = getIdentifier(parent);
    auto childId = mTree.value(id).at(row);
    return createIndex(row, column, childId);
}

template<class T, class Ptr>
QModelIndex ModelResult<T, Ptr>::createIndexFromId(const qint64 &id) const
{
    auto grandParentId = mParents.value(id, 0);
    auto row = mTree.value(grandParentId).indexOf(id);
    return createIndex(row, 0, id);
}

template<class T, class Ptr>
QModelIndex ModelResult<T, Ptr>::parent(const QModelIndex &index) const
{
    auto id = getIdentifier(index);
    auto parentId = mParents.value(id);
    return createIndexFromId(parentId);
}

template<class T, class Ptr>
bool ModelResult<T, Ptr>::canFetchMore(const QModelIndex &parent) const
{
    qDebug() << "Can fetch more: " << parent << mEntityChildrenFetched.value(parent.internalId());
    return !mEntityChildrenFetched.value(parent.internalId(), false);
}

template<class T, class Ptr>
void ModelResult<T, Ptr>::fetchMore(const QModelIndex &parent)
{
    qDebug() << "Fetch more: " << parent;
    fetchEntities(parent);
}

template<class T, class Ptr>
void ModelResult<T, Ptr>::add(const Ptr &value)
{
    auto childId = qHash(value->identifier());
    auto id = parentId(value);
    //Ignore updates we get before the initial fetch is done
    if (!mEntityChildrenFetched[id]) {
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
        Warning() << "Entity already in model " << value->identifier();
        return;
    }
    // qDebug() << "Inserting rows " << index << parent;
    beginInsertRows(QModelIndex(), index, index);
    mEntities.insert(childId, value);
    mTree[id].insert(index, childId);
    mParents.insert(childId, id);
    endInsertRows();
    // qDebug() << "Inserted rows " << mTree[id].size();
}


template<class T, class Ptr>
void ModelResult<T, Ptr>::remove(const Ptr &value)
{
    auto childId = qHash(value->identifier());
    auto id = parentId(value);
    auto parent = createIndexFromId(id);
    qDebug() << "Removed entity" << childId;
    auto index = mTree[id].indexOf(qHash(value->identifier()));
    beginRemoveRows(parent, index, index);
    mEntities.remove(childId);
    mTree[id].removeAll(childId);
    mParents.remove(childId);
    //TODO remove children
    endRemoveRows();
}

template<class T, class Ptr>
void ModelResult<T, Ptr>::fetchEntities(const QModelIndex &parent)
{
    const auto id = getIdentifier(parent);
    mEntityChildrenFetched[id] = true;
    Trace() << "Loading child entities";
    loadEntities(parent.data(DomainObjectRole).template value<Ptr>());
}

template<class T, class Ptr>
void ModelResult<T, Ptr>::setFetcher(const std::function<void(const Ptr &parent)> &fetcher)
{
    Trace() << "Setting fetcher";
    loadEntities = fetcher;
}

template<class T, class Ptr>
void ModelResult<T, Ptr>::setEmitter(const typename Akonadi2::ResultEmitter<Ptr>::Ptr &emitter)
{
    setFetcher(emitter->mFetcher);
    emitter->onAdded([this](const Ptr &value) {
        this->add(value);
    });
    emitter->onModified([this](const Ptr &value) {
        this->modify(value);
    });
    emitter->onRemoved([this](const Ptr &value) {
        this->remove(value);
    });
    emitter->onInitialResultSetComplete([this]() {
    });
    emitter->onComplete([this]() {
    });
    emitter->onClear([this]() {
    });
    mEmitter = emitter;
}

template<class T, class Ptr>
void ModelResult<T, Ptr>::modify(const Ptr &value)
{
    auto childId = qHash(value->identifier());
    auto id = parentId(value);
    //Ignore updates we get before the initial fetch is done
    if (!mEntityChildrenFetched[id]) {
        return;
    }
    auto parent = createIndexFromId(id);
    qDebug() << "Modified entity" << childId;
    auto i = mTree[id].indexOf(childId);
    mEntities.remove(childId);
    mEntities.insert(childId, value);
    //TODO check for change of parents
    auto idx = index(i, 0, parent);
    emit dataChanged(idx, idx);
}

template class ModelResult<Akonadi2::ApplicationDomain::Folder, Akonadi2::ApplicationDomain::Folder::Ptr>;
template class ModelResult<Akonadi2::ApplicationDomain::Mail, Akonadi2::ApplicationDomain::Mail::Ptr>;
template class ModelResult<Akonadi2::ApplicationDomain::Event, Akonadi2::ApplicationDomain::Event::Ptr>;
template class ModelResult<Akonadi2::ApplicationDomain::AkonadiResource, Akonadi2::ApplicationDomain::AkonadiResource::Ptr>;
