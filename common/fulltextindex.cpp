/*
 *   Copyright (C) 2018 Christian Mollekopf <mollekopf@kolabsys.com>
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
//xapian.h needs to be included first to build
#include <xapian.h>
#include "fulltextindex.h"

#include <QFile>
#include <QDir>

#include "log.h"
#include "definitions.h"

FulltextIndex::FulltextIndex(const QByteArray &resourceInstanceIdentifier, Sink::Storage::DataStore::AccessMode accessMode)
    : mName("fulltext"),
    mDbPath{QFile::encodeName(Sink::resourceStorageLocation(resourceInstanceIdentifier) + '/' + "fulltext")}
{
    try {
        if (QDir{}.mkpath(mDbPath)) {
            if (accessMode == Sink::Storage::DataStore::ReadWrite) {
                mDb = new Xapian::WritableDatabase(mDbPath.toStdString(), Xapian::DB_CREATE_OR_OPEN);
            } else {
                mDb = new Xapian::Database(mDbPath.toStdString(), Xapian::DB_OPEN);
            }
        } else {
            SinkError() << "Failed to open database" << mDbPath;
        }
    } catch (const Xapian::DatabaseError& e) {
        SinkError() << "Failed to open database" << mDbPath << ":" << QString::fromStdString(e.get_msg());
    }
}

FulltextIndex::~FulltextIndex()
{
    delete mDb;
}

static std::string idTerm(const QByteArray &key)
{
    return "Q" + key.toStdString();
}

void FulltextIndex::add(const QByteArray &key, const QString &value)
{
    add(key, {{{}, value}});
}

void FulltextIndex::add(const QByteArray &key, const QList<QPair<QString, QString>> &values)
{
    if (!mDb) {
        return;
    }
    Xapian::TermGenerator generator;
    Xapian::Document document;
    generator.set_document(document);

    for (const auto &entry : values) {
        if (!entry.second.isEmpty()) {
            generator.index_text(entry.second.toStdString());
        }
    }
    document.add_value(0, key.toStdString());

    const auto idterm = idTerm(key);
    document.add_boolean_term(idterm);

    writableDatabase()->replace_document(idterm, document);
}

void FulltextIndex::commitTransaction()
{
    if (mHasTransactionOpen) {
        Q_ASSERT(mDb);
        writableDatabase()->commit_transaction();
        mHasTransactionOpen = false;
    }
}

void FulltextIndex::abortTransaction()
{
    if (mHasTransactionOpen) {
        Q_ASSERT(mDb);
        writableDatabase()->cancel_transaction();
        mHasTransactionOpen = false;
    }
}

Xapian::WritableDatabase* FulltextIndex::writableDatabase()
{
    Q_ASSERT(dynamic_cast<Xapian::WritableDatabase*>(mDb));
    auto db = static_cast<Xapian::WritableDatabase*>(mDb);
    if (!mHasTransactionOpen) {
        db->begin_transaction();
        mHasTransactionOpen = true;
    }
    return db;
}

void FulltextIndex::remove(const QByteArray &key)
{
    if (!mDb) {
        return;
    }
    writableDatabase()->delete_document(idTerm(key));
}

QVector<QByteArray> FulltextIndex::lookup(const QString &searchTerm)
{
    if (!mDb) {
        return {};
    }
    QVector<QByteArray> results;

    try {
        Xapian::QueryParser parser;
        auto query = parser.parse_query(searchTerm.toStdString(), Xapian::QueryParser::FLAG_WILDCARD|Xapian::QueryParser::FLAG_PHRASE|Xapian::QueryParser::FLAG_BOOLEAN|Xapian::QueryParser::FLAG_LOVEHATE);
        Xapian::Enquire enquire(*mDb);
        enquire.set_query(query);

        auto limit = 1000;
        Xapian::MSet mset = enquire.get_mset(0, limit);
        Xapian::MSetIterator it = mset.begin();
        for (;it != mset.end(); it++) {
            auto doc = it.get_document();
            const auto data = doc.get_value(0);
            results << QByteArray{data.c_str(), int(data.length())};
        }
    }
    catch (const Xapian::Error &error) {
        // Nothing to do, move along
    }
    return results;
}

