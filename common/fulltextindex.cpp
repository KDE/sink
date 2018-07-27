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
    try {
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
    catch (const Xapian::Error &error) {
        SinkError() << "Exception during Xapian commit_transaction:" << error.get_msg().c_str();
        //FIXME we should somehow retry the transaction...
        Q_ASSERT(false);
    }
}

void FulltextIndex::commitTransaction()
{
    if (mHasTransactionOpen) {
        Q_ASSERT(mDb);
        try {
            writableDatabase()->commit_transaction();
            mHasTransactionOpen = false;
        }
        catch (const Xapian::Error &error) {
            SinkError() << "Exception during Xapian commit_transaction:" << error.get_msg().c_str();
            //FIXME we should somehow retry the transaction...
            Q_ASSERT(false);
        }
    }
}

void FulltextIndex::abortTransaction()
{
    if (mHasTransactionOpen) {
        Q_ASSERT(mDb);
        try {
            writableDatabase()->cancel_transaction();
            mHasTransactionOpen = false;
        }
        catch (const Xapian::Error &error) {
            SinkError() << "Exception during Xapian cancel_transaction:" << error.get_msg().c_str();
            //FIXME we should somehow retry the transaction...
            Q_ASSERT(false);
        }
    }
}

Xapian::WritableDatabase* FulltextIndex::writableDatabase()
{
    Q_ASSERT(dynamic_cast<Xapian::WritableDatabase*>(mDb));
    auto db = static_cast<Xapian::WritableDatabase*>(mDb);
    if (!mHasTransactionOpen) {
        try {
            db->begin_transaction();
            mHasTransactionOpen = true;
        }
        catch (const Xapian::Error &error) {
            SinkError() << "Exception during Xapian begin_transaction:" << error.get_msg().c_str();
            //FIXME we should somehow retry the transaction...
            Q_ASSERT(false);
        }
    }
    return db;
}

void FulltextIndex::remove(const QByteArray &key)
{
    if (!mDb) {
        return;
    }
    try {
        writableDatabase()->delete_document(idTerm(key));
    }
    catch (const Xapian::Error &error) {
        SinkError() << "Exception during Xapian delete_document:" << error.get_msg().c_str();
        //FIXME we should somehow retry the transaction...
        Q_ASSERT(false);
    }
}

QVector<QByteArray> FulltextIndex::lookup(const QString &searchTerm)
{
    if (!mDb) {
        return {};
    }
    QVector<QByteArray> results;

    try {
        Xapian::QueryParser parser;
        parser.set_database(*mDb);
        parser.set_max_expansion(100, Xapian::Query::WILDCARD_LIMIT_MOST_FREQUENT, Xapian::QueryParser::FLAG_PARTIAL);
        auto query = parser.parse_query(searchTerm.toStdString(), Xapian::QueryParser::FLAG_PHRASE|Xapian::QueryParser::FLAG_BOOLEAN|Xapian::QueryParser::FLAG_LOVEHATE|Xapian::QueryParser::FLAG_PARTIAL);
        SinkTrace() << "Running xapian query: " << QString::fromStdString(query.get_description());
        Xapian::Enquire enquire(*mDb);
        enquire.set_query(query);

        const auto limit = searchTerm.size() <= 4 ? 1000 : 10000;
        Xapian::MSet mset = enquire.get_mset(0, limit);
        SinkTrace() << "Found " << mset.size() << " results, limited to " << limit;
        for (Xapian::MSetIterator it = mset.begin(); it != mset.end(); it++) {
            auto doc = it.get_document();
            const auto data = doc.get_value(0);
            results << QByteArray{data.c_str(), int(data.length())};
        }
    }
    catch (const Xapian::Error &) {
        // Nothing to do, move along
    }
    return results;
}

qint64 FulltextIndex::getDoccount() const
{
    if (!mDb) {
        return -1;
    }
    try {
        return mDb->get_doccount();
    } catch (const Xapian::Error &) {
        // Nothing to do, move along
    }
    return  -1;
}

FulltextIndex::Result FulltextIndex::getIndexContent(const QByteArray &identifier) const
{
    if (!mDb) {
        {};
    }
    try {
        auto id = "Q" + identifier.toStdString();
        Xapian::PostingIterator p = mDb->postlist_begin(id);
        if (p != mDb->postlist_end(id)) {
            auto document = mDb->get_document(*p);
            QStringList terms;
            for (auto it = document.termlist_begin(); it != document.termlist_end(); it++) {
                terms << QString::fromStdString(*it);
            }
            return {true, terms};
        }
    } catch (const Xapian::Error &) {
        // Nothing to do, move along
    }
    return {};
}
