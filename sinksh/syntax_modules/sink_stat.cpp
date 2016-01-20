/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <QDebug>
#include <QObject> // tr()
#include <QTimer>
#include <QDir>

#include "common/resource.h"
#include "common/storage.h"
#include "common/domain/event.h"
#include "common/domain/folder.h"
#include "common/resourceconfig.h"
#include "common/log.h"
#include "common/storage.h"
#include "common/definitions.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkStat
{

void statResources(const QStringList &resources, const State &state)
{
    qint64 total = 0;
    for (const auto &resource : resources) {
        Sink::Storage storage(Sink::storageLocation(), resource, Sink::Storage::ReadOnly);
        auto transaction = storage.createTransaction(Sink::Storage::ReadOnly);

        QList<QByteArray> databases = transaction.getDatabaseNames();
        for (const auto &databaseName : databases) {
            state.printLine(QObject::tr("Database: %1").arg(QString(databaseName)), 1);
            auto db = transaction.openDatabase(databaseName);
            qint64 size = db.getSize() / 1024;
            state.printLine(QObject::tr("Size [kb]: %1").arg(size), 1);
            total += size;
        }
        int diskUsage = 0;

        QDir dir(Sink::storageLocation());
        for (const auto &folder : dir.entryList(QStringList() << resource + "*")) {
            diskUsage += Sink::Storage(Sink::storageLocation(), folder, Sink::Storage::ReadOnly).diskUsage();
        }
        auto size = diskUsage / 1024;
        state.printLine(QObject::tr("Disk usage [kb]: %1").arg(size), 1);
    }

    state.printLine(QObject::tr("Total [kb]: %1").arg(total));
}

bool statAllResources(State &state)
{
    Sink::Query query;
    query.liveQuery = false;
    auto model = SinkshUtils::loadModel("resource", query);

    //SUUUPER ugly, but can't think of a better way with 2 glasses of wine in me on Christmas day
    static QStringList resources;
    resources.clear();

    QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, [model](const QModelIndex &index, int start, int end) mutable {
        for (int i = start; i <= end; i++) {
            auto object = model->data(model->index(i, 0, index), Sink::Store::DomainObjectBaseRole).value<Sink::ApplicationDomain::ApplicationDomainType::Ptr>();
            resources << object->identifier();
        }
    });

    QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [model, state](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
        if (roles.contains(Sink::Store::ChildrenFetchedRole)) {
            statResources(resources, state);
            state.commandFinished();
        }
    });

    if (!model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool()) {
        return true;
    }

    return false;
}

bool stat(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        return statAllResources(state);
    }

    statResources(args, state);
    return false;
}

Syntax::List syntax()
{
    Syntax state("stat", QObject::tr("Shows database usage for the resources requested"), &SinkStat::stat, Syntax::EventDriven);
    state.completer = &SinkshUtils::resourceCompleter;

    return Syntax::List() << state;
}

REGISTER_SYNTAX(SinkStat)

}
