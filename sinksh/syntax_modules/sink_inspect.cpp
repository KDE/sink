/*
 *   Copyright (C) 2017 Christian Mollekopf <mollekopf@kolabsys.com>
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
#include "common/entitybuffer.h"
#include "common/metadata_generated.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkInspect
{

bool inspect(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("Options: $type [--resource $resource] [--db $db] [--filter $id]"));
    }
    auto options = SyntaxTree::parseOptions(args);
    auto resource = options.options.value("resource").value(0);

    Sink::Storage::DataStore storage(Sink::storageLocation(), resource, Sink::Storage::DataStore::ReadOnly);
    auto transaction = storage.createTransaction(Sink::Storage::DataStore::ReadOnly);

    auto dbs = options.options.value("db");
    auto idFilter = options.options.value("filter");

    auto databases = transaction.getDatabaseNames();
    if (dbs.isEmpty()) {
        state.printLine(QString("Available databases: ") + databases.join(", "));
        return false;
    }
    auto dbName = dbs.value(0).toUtf8();
    auto isMainDb = dbName.contains(".main");
    if (!databases.contains(dbName)) {
        state.printError(QString("Database not available: ") + dbName);
    }

    state.printLine(QString("Opening: ") + dbName);
    auto db = transaction.openDatabase(dbName,
            [&] (const Sink::Storage::DataStore::Error &e) {
                Q_ASSERT(false);
                state.printError(e.message);
            }, false);
    QByteArray filter;
    if (!idFilter.isEmpty()) {
        filter = idFilter.first().toUtf8();
    }
    bool findSubstringKeys = !filter.isEmpty();
    auto count = db.scan(filter, [&] (const QByteArray &key, const QByteArray &data) {
                if (isMainDb) {
                    Sink::EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
                    if (!buffer.isValid()) {
                        state.printError("Read invalid buffer from disk: " + key);
                    } else {
                        const auto metadata = flatbuffers::GetRoot<Sink::Metadata>(buffer.metadataBuffer());
                        state.printLine("Key: " + key + " Operation: " + QString::number(metadata->operation()));
                    }
                } else {
                    state.printLine("Key: " + key);
                }
                return true;
            },
            [&](const Sink::Storage::DataStore::Error &e) {
                state.printError(e.message);
            },
            findSubstringKeys);

    state.printLine("Found " + QString::number(count) + " entries");
    return false;
}

Syntax::List syntax()
{
    Syntax state("inspect", QObject::tr("Inspect database for the resource requested"), &SinkInspect::inspect, Syntax::NotInteractive);
    state.completer = &SinkshUtils::resourceCompleter;

    return Syntax::List() << state;
}

REGISTER_SYNTAX(SinkInspect)

}
