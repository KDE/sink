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

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QDebug>
#include "query.h"
#include "clientapi.h"

#include "resultprovider.h"

template<class T>
class ModelResult : public QAbstractItemModel
{
public:

    enum Roles {
        DomainObjectRole = Qt::UserRole + 1
    };

    ModelResult(const Akonadi2::Query &query, const QList<QByteArray> &propertyColumns)
        :QAbstractItemModel(),
        mPropertyColumns(propertyColumns)
    {
    }

    static qint64 getIdentifier(const QModelIndex &idx)
    {
        if (!idx.isValid()) {
            return 0;
        }
        return idx.internalId();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const
    {
        return mTree[getIdentifier(parent)].size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const
    {
        return mPropertyColumns.size();
    }

    virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const
    {
        if (role == DomainObjectRole) {
            qWarning() << "trying to get entity " << index.internalId();
            Q_ASSERT(mEntities.contains(index.internalId()));
            return QVariant::fromValue(mEntities.value(index.internalId()));
        }
        qDebug() << "Invalid role";
        return QVariant();
    }

    QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const
    {
        auto id = getIdentifier(parent);
        auto childId = mTree.value(id).at(row);
        return createIndex(row, column, childId);
    }

    QModelIndex parent(const QModelIndex &index) const
    {
        auto id = getIdentifier(index);
        auto parentId = mParents.value(id);
        auto grandParentId = mParents.value(parentId, 0);
        auto row = mTree.value(grandParentId).indexOf(parentId);
        return createIndex(row, 0, parentId);
    }

    bool canFetchMore(const QModelIndex &parent) const
    {
        return mEntityChildrenFetched.value(parent.internalId());
    }

    void fetchMore(const QModelIndex &parent)
    {
        fetchEntities(parent);
    }

    void fetchEntities(const QModelIndex &parent)
    {
        qDebug() << "Fetching entities";
        const auto id = getIdentifier(parent);
        // beginResetModel();
        // mEntities.remove(id);
        mEntityChildrenFetched[id] = true;
        auto query = mQuery;
        if (!parent.isValid()) {
            qDebug() << "no parent";
            query.propertyFilter.insert("parent", QByteArray());
        } else {
            qDebug() << "parent is valid";
            auto object = parent.data(DomainObjectRole).template value<typename T::Ptr>();
            Q_ASSERT(object);
            query.propertyFilter.insert("parent", object->identifier());
        }
        auto emitter = Akonadi2::Store::load<T>(query);
        emitter->onAdded([this, id, parent](const typename T::Ptr &value) {
            auto childId = qHash(value->identifier());
            qDebug() << "Added entity " << childId;
            const auto keys = mTree[id];
            int index = 0;
            for (; index < keys.size(); index++) {
                if (childId < keys.at(index)) {
                    break;
                }
            }
            beginInsertRows(parent, index, index);
            mEntities.insert(childId, value);
            mTree[id].insert(index, childId);
            mParents.insert(childId, id);
            endInsertRows();
        });
        emitter->onModified([this, id, parent](const typename T::Ptr &value) {
            auto childId = qHash(value->identifier());
            qDebug() << "Modified entity" << childId;
            auto i = mTree[id].indexOf(childId);
            mEntities.remove(childId);
            mEntities.insert(childId, value);
            //TODO check for change of parents
            auto idx = index(i, 0, parent);
            emit dataChanged(idx, idx);
        });
        emitter->onRemoved([this, id, parent](const typename T::Ptr &value) {
            auto childId = qHash(value->identifier());
            qDebug() << "Removed entity" << childId;
            auto index = mTree[id].indexOf(qHash(value->identifier()));
            beginRemoveRows(parent, index, index);
            mEntities.remove(childId);
            mTree[id].removeAll(childId);
            mParents.remove(childId);
            //TODO remove children
            endRemoveRows();
        });
        emitter->onInitialResultSetComplete([this]() {
        });
        emitter->onComplete([this, id]() {
            mEmitter[id].clear();
        });
        emitter->onClear([this]() {
            // beginResetModel();
            // mEntities.clear();
            // endResetModel();
        });
        mEmitter.insert(id, emitter);
        // endResetModel();
    }

private:
    QMap<qint64 /* parent entity id */, QSharedPointer<Akonadi2::ResultEmitter<typename T::Ptr> >> mEmitter;
    //TODO we should be able to directly use T as index, with an appropriate hash function, and thus have a QMap<T, T> and QList<T>
    QMap<qint64 /* entity id */, typename T::Ptr> mEntities;
    QMap<qint64 /* parent entity id */, QList<qint64> /* child entity id*/> mTree;
    QMap<qint64 /* child entity id */, qint64 /* parent entity id*/> mParents;
    QMap<qint64 /* entity id */, bool> mEntityChildrenFetched;
    QList<QByteArray> mPropertyColumns;
    Akonadi2::Query mQuery;
};

