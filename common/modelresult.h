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
#include "log.h"
#include "resultprovider.h"
#include "threadboundary.h"

namespace Sink {
class Notifier;
}

template <class T, class Ptr>
class ModelResult : public QAbstractItemModel
{
public:
    //Update the copy in store.h as well if you modify this
    enum Roles
    {
        DomainObjectRole = Qt::UserRole + 1,
        ChildrenFetchedRole,
        DomainObjectBaseRole,
        StatusRole, //ApplicationDomain::SyncStatus
        WarningRole, //ApplicationDomain::Warning, only if status == warning || status == error
        ProgressRole //ApplicationDomain::Progress
    };

    ModelResult(const Sink::Query &query, const QList<QByteArray> &propertyColumns, const Sink::Log::Context &);
    ~ModelResult();

    void setEmitter(const typename Sink::ResultEmitter<Ptr>::Ptr &);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;

    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    void setFetcher(const std::function<void()> &fetcher);

private:
    void add(const Ptr &value);
    void modify(const Ptr &value);
    void remove(const Ptr &value);
    bool childrenFetched(const QModelIndex &) const;

    qint64 parentId(const Ptr &value);
    QModelIndex createIndexFromId(const qint64 &id) const;
    bool allParentsAvailable(qint64 id) const;

    Sink::Log::Context  mLogCtx;
    // TODO we should be able to directly use T as index, with an appropriate hash function, and thus have a QMap<T, T> and QList<T>
    QMap<qint64 /* entity id */, Ptr> mEntities;
    QMap<qint64 /* parent entity id */, QList<qint64> /* child entity id*/> mTree;
    QMap<qint64 /* child entity id */, qint64 /* parent entity id*/> mParents;
    QMap<qint64 /* entity id */, int /* Status */> mEntityStatus;
    bool mFetchInProgress{false};
    bool mFetchedAll{false};
    bool mFetchComplete{false};
    QList<QByteArray> mPropertyColumns;
    Sink::Query mQuery;
    std::function<void()> loadEntities;
    typename Sink::ResultEmitter<Ptr>::Ptr mEmitter;
    async::ThreadBoundary threadBoundary;
    QScopedPointer<Sink::Notifier> mNotifier;
};
