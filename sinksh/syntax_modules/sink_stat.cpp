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
    for (const auto &resource : resources) {
        qint64 total = 0;
        Sink::Storage::DataStore storage(Sink::storageLocation(), resource, Sink::Storage::DataStore::ReadOnly);
        auto transaction = storage.createTransaction(Sink::Storage::DataStore::ReadOnly);

        QList<QByteArray> databases = transaction.getDatabaseNames();
        for (const auto &databaseName : databases) {
            state.printLine(QObject::tr("Database: %1").arg(QString(databaseName)), 1);
            auto db = transaction.openDatabase(databaseName);
            qint64 size = db.getSize() / 1024;
            state.printLine(QObject::tr("Size [kb]: %1").arg(size), 1);
            total += size;
        }
        state.printLine(QObject::tr("Resource total in database [kb]: %1").arg(total), 1);
        int diskUsage = 0;

        QDir dir(Sink::storageLocation());
        for (const auto &folder : dir.entryList(QStringList() << resource + "*")) {
            state.printLine(QObject::tr("Accumulating %1").arg(folder), 1);
            diskUsage += Sink::Storage::DataStore(Sink::storageLocation(), folder, Sink::Storage::DataStore::ReadOnly).diskUsage();
        }
        auto size = diskUsage / 1024;
        state.printLine(QObject::tr("Actual database file sizes [kb]: %1").arg(size), 1);
    }

}

bool statAllResources(State &state)
{
    Sink::Query query;
    QStringList resources;
    for (const auto &r : SinkshUtils::getStore("resource").read(query)) {
        resources << r.identifier();
    }
    statResources(resources, state);
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
    Syntax state("stat", QObject::tr("Shows database usage for the resources requested"), &SinkStat::stat, Syntax::NotInteractive);
    state.completer = &SinkshUtils::resourceCompleter;

    return Syntax::List() << state;
}

REGISTER_SYNTAX(SinkStat)

}
