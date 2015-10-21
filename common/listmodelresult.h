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

#include "common/resultprovider.h"

enum Roles {
    DomainObjectRole = Qt::UserRole + 1
};

template<class T>
class ListModelResult : public QAbstractListModel
{
public:

    ListModelResult(const QSharedPointer<Akonadi2::ResultEmitter<T> > &emitter, const QByteArray &property)
        :QAbstractListModel(),
        mEmitter(emitter),
        mProperty(property)
    {
        emitter->onAdded([this, property](const T &value) {
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
        emitter->onModified([this, property](const T &value) {
            mEntities.remove(value->identifier());
            mEntities.insert(value->identifier(), value);
            //FIXME
            // emit dataChanged();
        });
        emitter->onRemoved([this, property](const T &value) {
            auto index = mEntities.keys().indexOf(value->identifier());
            beginRemoveRows(QModelIndex(), index, index);
            mEntities.remove(value->identifier());
            endRemoveRows();
        });
        emitter->onInitialResultSetComplete([this]() {
        });
        emitter->onComplete([this]() {
            // qDebug() << "COMPLETE";
            mEmitter.clear();
        });
        emitter->onClear([this]() {
            // qDebug() << "CLEAR";
            beginResetModel();
            mEntities.clear();
            endResetModel();
        });
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const
    {
        return mEntities.size();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const
    {
        if (index.row() >= mEntities.size()) {
            qWarning() << "Out of bounds access";
            return QVariant();
        }
        if (role == Qt::DisplayRole) {
            auto entity = mEntities.value(mEntities.keys().at(index.row()));
            return entity->getProperty(mProperty).toString() + entity->identifier();
        }
        if (role == DomainObjectRole) {
            return QVariant::fromValue(mEntities.value(mEntities.keys().at(index.row())));
        }
        return QVariant();
    }

private:
    QSharedPointer<Akonadi2::ResultEmitter<T> > mEmitter;
    QMap<QByteArray, T> mEntities;
    QByteArray mProperty;
};

