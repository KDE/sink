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

void statResource(const QString &resource, const State &state)
{
    state.printLine("Resource " + resource + ":");
    qint64 total = 0;
    Sink::Storage::DataStore storage(Sink::storageLocation(), resource, Sink::Storage::DataStore::ReadOnly);
    auto transaction = storage.createTransaction(Sink::Storage::DataStore::ReadOnly);

    QList<QByteArray> databases = transaction.getDatabaseNames();
    for (const auto &databaseName : databases) {
        auto db = transaction.openDatabase(databaseName);
        qint64 size = db.getSize() / 1024;
        state.printLine(QObject::tr("%1:\t%2 [kb]").arg(QString(databaseName)).arg(size), 1);
        total += size;
    }
    state.printLine();
    state.printLine(QObject::tr("Calculated named database sizes total of main database: %1 [kb]").arg(total), 1);

    auto stat = transaction.stat(false);
    state.printLine(QObject::tr("Total calculated free size [kb]: %1").arg(stat.freePages * stat.pageSize / 1024), 1);
    state.printLine(QObject::tr("Write amplification of main database: %1").arg(double(storage.diskUsage() / 1024)/double(total)), 1);
    int diskUsage = 0;

    state.printLine();
    QDir dir(Sink::storageLocation());
    for (const auto &folder : dir.entryList(QStringList() << resource + "*")) {
        auto size = Sink::Storage::DataStore(Sink::storageLocation(), folder, Sink::Storage::DataStore::ReadOnly).diskUsage();
        diskUsage += size;
        state.printLine(QObject::tr("... accumulating %1: %2 [kb]").arg(folder).arg(size / 1024), 1);
    }
    auto size = diskUsage / 1024;
    state.printLine(QObject::tr("Actual database file sizes total: %1 [kb]").arg(size), 1);

    QDir dataDir{Sink::resourceStorageLocation(resource.toLatin1()) + "/fulltext/"};
    Q_ASSERT(dataDir.exists());
    qint64 dataSize = 0;
    for (const auto &e : dataDir.entryInfoList(QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot)) {
        dataSize += e.size();
    }
    state.printLine(QObject::tr("Fulltext index size [kb]: %1").arg(dataSize / 1024), 1);

    state.printLine();
}

bool statAllResources(State &state)
{
    Sink::Query query;
    for (const auto &r : SinkshUtils::getStore("resource").read(query)) {
        statResource(SinkshUtils::parseUid(r.identifier()), state);
    }
    return false;
}

bool stat(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        return statAllResources(state);
    }

    for (const auto &r : args) {
        statResource(SinkshUtils::parseUid(r.toUtf8()), state);
    }
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
