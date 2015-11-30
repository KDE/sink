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
#include <QSharedPointer>
#include <functional>
#include "query.h"
#include "resultprovider.h"

template<class T, class Ptr>
class ModelResult : public QAbstractItemModel
{
public:
    enum Roles {
        DomainObjectRole = Qt::UserRole + 1
    };

    ModelResult(const Akonadi2::Query &query, const QList<QByteArray> &propertyColumns);

    void setEmitter(const typename Akonadi2::ResultEmitter<Ptr>::Ptr &);

    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    int columnCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
    QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const;
    QModelIndex parent(const QModelIndex &index) const;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const;

    bool canFetchMore(const QModelIndex &parent) const;
    void fetchMore(const QModelIndex &parent);

    void add(const Ptr &value);
    void modify(const Ptr &value);
    void remove(const Ptr &value);

    void setFetcher(const std::function<void(const Ptr &parent)> &fetcher);

private:
    qint64 parentId(const Ptr &value);
    QModelIndex createIndexFromId(const qint64 &id) const;
    void fetchEntities(const QModelIndex &parent);

    //TODO we should be able to directly use T as index, with an appropriate hash function, and thus have a QMap<T, T> and QList<T>
    QMap<qint64 /* entity id */, Ptr> mEntities;
    QMap<qint64 /* parent entity id */, QList<qint64> /* child entity id*/> mTree;
    QMap<qint64 /* child entity id */, qint64 /* parent entity id*/> mParents;
    QMap<qint64 /* entity id */, bool> mEntityChildrenFetched;
    QList<QByteArray> mPropertyColumns;
    Akonadi2::Query mQuery;
    std::function<void(const Ptr &)> loadEntities;
    typename Akonadi2::ResultEmitter<Ptr>::Ptr mEmitter;
};

