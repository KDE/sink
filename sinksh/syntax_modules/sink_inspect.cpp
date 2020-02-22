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
#include <QFile>

#include "common/resource.h"
#include "common/storage.h"
#include "common/resourceconfig.h"
#include "common/log.h"
#include "common/storage.h"
#include "common/definitions.h"
#include "common/entitybuffer.h"
#include "common/metadata_generated.h"
#include "common/bufferutils.h"
#include "common/fulltextindex.h"

#include "storage/key.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkInspect
{

using Sink::Storage::Key;
using Sink::Storage::Identifier;
using Sink::Storage::Revision;

QString parse(const QByteArray &bytes)
{
    if (Revision::isValidInternal(bytes)) {
        return Revision::fromInternalByteArray(bytes).toDisplayString();
    } else if (Key::isValidInternal(bytes)) {
        return Key::fromInternalByteArray(bytes).toDisplayString();
    } else if (Identifier::isValidInternal(bytes)) {
        return Identifier::fromInternalByteArray(bytes).toDisplayString();
    } else {
        return QString::fromUtf8(bytes);
    }
}

static QString operationName(int operation)
{
    switch (operation) {
        case 1: return "Create";
        case 2: return "Modify";
        case 3: return "Delete";
    }
    return {};
}

Syntax::List syntax();

bool inspect(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        //state.printError(QObject::tr("Options: [--resource $resource] ([--db $db] [--filter $id] | [--validaterids $type] | [--fulltext [$id]])"));
        state.printError(syntax()[0].usage());
        return false;
    }
    auto options = SyntaxTree::parseOptions(args);
    auto resource = SinkshUtils::parseUid(options.options.value("resource").value(0).toUtf8());

    Sink::Storage::DataStore storage(Sink::storageLocation(), resource, Sink::Storage::DataStore::ReadOnly);
    auto transaction = storage.createTransaction(Sink::Storage::DataStore::ReadOnly);

    if (options.options.contains("validaterids")) {
        if (options.options.value("validaterids").isEmpty()) {
            state.printError(QObject::tr("Specify a type to validate."));
            return false;
        }
        auto type = options.options.value("validaterids").first().toUtf8();
        /*
         * Try to find all rid's for all uid's.
         * If we have entities without rid's that either means we have only created it locally or that we have a problem.
         */
        Sink::Storage::DataStore syncStore(Sink::storageLocation(), resource + ".synchronization", Sink::Storage::DataStore::ReadOnly);
        auto syncTransaction = syncStore.createTransaction(Sink::Storage::DataStore::ReadOnly);

        auto db = transaction.openDatabase(type + ".main",
                [&] (const Sink::Storage::DataStore::Error &e) {
                    Q_ASSERT(false);
                    state.printError(e.message);
                }, Sink::Storage::IntegerKeys);

        auto ridMap = syncTransaction.openDatabase("localid.mapping." + type,
                [&] (const Sink::Storage::DataStore::Error &e) {
                    Q_ASSERT(false);
                    state.printError(e.message);
                });

        QHash<QByteArray, QByteArray> hash;

        ridMap.scan("", [&] (const QByteArray &key, const QByteArray &data) {
                    hash.insert(key, data);
                    return true;
                },
                [&](const Sink::Storage::DataStore::Error &e) {
                    state.printError(e.message);
                },
                false);

        QSet<QByteArray> uids;
        db.scan("", [&] (const QByteArray &key, const QByteArray &data) {
                    size_t revision = Sink::byteArrayToSizeT(key);
                    uids.insert(Sink::Storage::DataStore::getUidFromRevision(transaction, revision).toDisplayByteArray());
                    return true;
                },
                [&](const Sink::Storage::DataStore::Error &e) {
                    state.printError(e.message);
                },
                false);

        int missing = 0;
        for (const auto &uid : uids) {
            if (!hash.remove(uid)) {
                missing++;
                qWarning() << "Failed to find RID for " << uid;
            }
        }
        if (missing) {
            qWarning() << "Found a total of " << missing << " missing rids";
        }

        //If we still have items in the hash it means we have rid mappings for entities
        //that no longer exist.
        if (!hash.isEmpty()) {
            qWarning() << "Have rids left: " << hash.size();
        } else if (!missing) {
            qWarning() << "Everything is in order.";
        }

        return false;
    }
    if (options.options.contains("fulltext")) {
        FulltextIndex index(resource, Sink::Storage::DataStore::ReadOnly);
        if (options.options.value("fulltext").isEmpty()) {
            state.printLine(QString("Total document count: ") + QString::number(index.getDoccount()));
        } else {
            const auto entityId = SinkshUtils::parseUid(options.options.value("fulltext").first().toUtf8());
            const auto content = index.getIndexContent(entityId);
            if (!content.found) {
                state.printLine(QString("Failed to find the document with the id: ") + entityId);
            } else {
                state.printLine(QString("Found document with terms: ") + content.terms.join(", "), 1);
            }

        }
        return false;
    }

    auto dbs = options.options.value("db");
    auto idFilter = options.options.value("filter");

    state.printLine(QString("Current revision: %1").arg(Sink::Storage::DataStore::maxRevision(transaction)));
    state.printLine(QString("Last clean revision: %1").arg(Sink::Storage::DataStore::cleanedUpRevision(transaction)));

    auto databases = transaction.getDatabaseNames();
    if (dbs.isEmpty()) {
        state.printLine("Available databases: ");
        for (const auto &db : databases) {
            state.printLine(db, 1);
        }
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
            });

    QByteArray filter;
    if (!idFilter.isEmpty()) {
        filter = idFilter.first().toUtf8();
    }

    //Print rest of db
    bool findSubstringKeys = !filter.isEmpty();
    int keySizeTotal = 0;
    int valueSizeTotal = 0;
    auto count = db.scan(filter, [&] (const QByteArray &key, const QByteArray &data) {
                keySizeTotal += key.size();
                valueSizeTotal += data.size();

                const auto parsedKey = parse(key);

                if (isMainDb) {
                    Sink::EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
                    if (!buffer.isValid()) {
                        state.printError("Read invalid buffer from disk: " + parsedKey);
                    } else {
                        const auto metadata = flatbuffers::GetRoot<Sink::Metadata>(buffer.metadataBuffer());
                        state.printLine("Key: " + parsedKey
                                        + " Operation: " + operationName(metadata->operation())
                                        + " Replay: " + (metadata->replayToSource() ? "true" : "false")
                                        + ((metadata->modifiedProperties() && metadata->modifiedProperties()->size() != 0) ? (" [" + Sink::BufferUtils::fromVector(*metadata->modifiedProperties()).join(", ")) + "]": "")
                                        + " Value size: " + QString::number(data.size())
                                        );
                    }
                } else {
                    state.printLine("Key: " + parsedKey + "\tValue: " + parse(data));
                }
                return true;
            },
            [&](const Sink::Storage::DataStore::Error &e) {
                state.printError(e.message);
            },
            findSubstringKeys);

    state.printLine("Found " + QString::number(count) + " entries");
    state.printLine("Keys take up " + QString::number(keySizeTotal) + " bytes => " + QString::number(keySizeTotal/1024) + " kb");
    state.printLine("Values take up " + QString::number(valueSizeTotal) + " bytes => " + QString::number(valueSizeTotal/1024) + " kb");
    return false;
}

Syntax::List syntax()
{
    Syntax state("inspect", QObject::tr("Inspect database for the resource requested"),
        &SinkInspect::inspect, Syntax::NotInteractive);

    state.addParameter("resource", {"resource", "Which resource to inspect", true});
    state.addParameter("db", {"database", "Which database to inspect"});
    state.addParameter("filter", {"id", "A specific id to filter the results by (currently not working)"});
    state.addParameter("validaterids", {"type", "Validate remote Ids of the given type"});
    state.addParameter("fulltext", {"id", "If 'id' is not given, count the number of fulltext documents. Else, print the terms of the document with the given id"});

    state.completer = &SinkshUtils::resourceCompleter;

    return Syntax::List() << state;
}

REGISTER_SYNTAX(SinkInspect)

}
