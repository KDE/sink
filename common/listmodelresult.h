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
#include <QAbstractListModel>
#include <QDebug>

#include "resultprovider.h"

enum Roles {
    DomainObjectRole = Qt::UserRole + 1
};

template<class T>
class ListModelResult : public QAbstractListModel
{
public:

    ListModelResult(const QList<QByteArray> &propertyColumns)
        :QAbstractListModel(),
        mPropertyColumns(propertyColumns)
    {
    }

    ListModelResult(const QSharedPointer<Akonadi2::ResultEmitter<T> > &emitter, const QList<QByteArray> &propertyColumns)
        :QAbstractListModel(),
        mPropertyColumns(propertyColumns)
    {
        setEmitter(emitter);
    }

    void setEmitter(const QSharedPointer<Akonadi2::ResultEmitter<T> > &emitter)
    {
        beginResetModel();
        mEntities.clear();
        mEmitter = emitter;
        emitter->onAdded([this](const T &value) {
            const auto keys = mEntities.keys();
            int index = 0;
            for (; index < keys.size(); index++) {
                if (value->identifier() < keys.at(index)) {
                    break;
                }
            }
            beginInsertRows(QModelIndex(), index, index);
            mEntities.insert(value->identifier(), value);
            endInsertRows();
        });
        emitter->onModified([this](const T &value) {
            auto i = mEntities.keys().indexOf(value->identifier());
            mEntities.remove(value->identifier());
            mEntities.insert(value->identifier(), value);
            auto idx = index(i, 0, QModelIndex());
            emit dataChanged(idx, idx);
        });
        emitter->onRemoved([this](const T &value) {
            auto index = mEntities.keys().indexOf(value->identifier());
            beginRemoveRows(QModelIndex(), index, index);
            mEntities.remove(value->identifier());
            endRemoveRows();
        });
        emitter->onInitialResultSetComplete([this]() {
        });
        emitter->onComplete([this]() {
            mEmitter.clear();
        });
        emitter->onClear([this]() {
            beginResetModel();
            mEntities.clear();
            endResetModel();
        });
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const
    {
        return mEntities.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const
    {
        return mPropertyColumns.size();
    }

    virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const
    {
        if (index.row() >= mEntities.size()) {
            qWarning() << "Out of bounds access";
            return QVariant();
        }
        if (role == Qt::DisplayRole) {
            if (index.column() < mPropertyColumns.size()) {
                auto entity = mEntities.value(mEntities.keys().at(index.row()));
                return entity->getProperty(mPropertyColumns.at(index.column())).toString();
            }
        }
        if (role == DomainObjectRole) {
            return QVariant::fromValue(mEntities.value(mEntities.keys().at(index.row())));
        }
        return QVariant();
    }

private:
    QSharedPointer<Akonadi2::ResultEmitter<T> > mEmitter;
    QMap<QByteArray, T> mEntities;
    QList<QByteArray> mPropertyColumns;
};

