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

#include "resultprovider.h"

template<class T, class Ptr>
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

    QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const
    {
        auto id = getIdentifier(parent);
        auto childId = mTree.value(id).at(row);
        return createIndex(row, column, childId);
    }

    QModelIndex createIndexFromId(const qint64 &id) const
    {
        auto grandParentId = mParents.value(id, 0);
        auto row = mTree.value(grandParentId).indexOf(id);
        return createIndex(row, 0, id);
    }

    QModelIndex parent(const QModelIndex &index) const
    {
        auto id = getIdentifier(index);
        auto parentId = mParents.value(id);
        return createIndexFromId(parentId);
    }

    bool canFetchMore(const QModelIndex &parent) const
    {
        return mEntityChildrenFetched.value(parent.internalId());
    }

    void fetchMore(const QModelIndex &parent)
    {
        fetchEntities(parent);
    }

    qint64 parentId(const Ptr &value)
    {
        return qHash(value->getProperty("parent").toByteArray());
    }

    void add(const Ptr &value)
    {
        auto childId = qHash(value->identifier());
        auto id = parentId(value);
        //Ignore updates we get before the initial fetch is done
        if (!mEntityChildrenFetched[id]) {
            return;
        }
        auto parent = createIndexFromId(id);
        qDebug() << "Added entity " << childId << value->identifier();
        const auto keys = mTree[id];
        int index = 0;
        for (; index < keys.size(); index++) {
            if (childId < keys.at(index)) {
                break;
            }
        }
        if (mEntities.contains(childId)) {
            qWarning() << "Entity already in model " << value->identifier();
            return;
        }
        beginInsertRows(QModelIndex(), index, index);
        mEntities.insert(childId, value);
        mTree[id].insert(index, childId);
        mParents.insert(childId, id);
        endInsertRows();
    }

    void modify(const Ptr &value)
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

    void remove(const Ptr &value)
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

    void fetchEntities(const QModelIndex &parent)
    {
        const auto id = getIdentifier(parent);
        mEntityChildrenFetched[id] = true;
        Trace() << "Loading child entities";
        loadEntities(parent.data(DomainObjectRole).template value<Ptr>());
    }

    void setFetcher(const std::function<void(const Ptr &parent)> &fetcher)
    {
        Trace() << "Setting fetcher";
        loadEntities = fetcher;
    }

private:
    //TODO we should be able to directly use T as index, with an appropriate hash function, and thus have a QMap<T, T> and QList<T>
    QMap<qint64 /* entity id */, Ptr> mEntities;
    QMap<qint64 /* parent entity id */, QList<qint64> /* child entity id*/> mTree;
    QMap<qint64 /* child entity id */, qint64 /* parent entity id*/> mParents;
    QMap<qint64 /* entity id */, bool> mEntityChildrenFetched;
    QList<QByteArray> mPropertyColumns;
    Akonadi2::Query mQuery;
    std::function<void(const Ptr &)> loadEntities;
};

