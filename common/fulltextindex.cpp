#include <xapian.h>

#include "fulltextindex.h"

#include "log.h"
#include "definitions.h"
#include <QFile>
#include <QDir>

SINK_DEBUG_AREA("fulltextIndex")

FulltextIndex::FulltextIndex(const QByteArray &resourceInstanceIdentifier, const QByteArray &name, Sink::Storage::DataStore::AccessMode accessMode)
    : mName(name),
    mDbPath{QFile::encodeName(Sink::resourceStorageLocation(resourceInstanceIdentifier) + '/' + name)}
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
    Q_ASSERT(mDb);
    Xapian::TermGenerator generator;
    /* generator.set_stemmer(Xapian::Stem("en")); */

    Xapian::Document document;
    generator.set_document(document);

    generator.index_text(value.toStdString());
    /* generator.increase_termpos(1); */

    document.add_value(0, key.toStdString());

    const auto idterm = idTerm(key);
    document.add_boolean_term(idTerm(key));

    auto db = static_cast<Xapian::WritableDatabase*>(mDb);
    if (!mHasTransactionOpen) {
        db->begin_transaction();
        mHasTransactionOpen = true;
    }
    db->replace_document(idterm, document);
}

void FulltextIndex::commitTransaction()
{
    if (mHasTransactionOpen) {
        Q_ASSERT(mDb);
        mHasTransactionOpen = false;
        auto db = static_cast<Xapian::WritableDatabase*>(mDb);
        db->commit_transaction();
    }
}

void FulltextIndex::abortTransaction()
{
    if (mHasTransactionOpen) {
        Q_ASSERT(mDb);
        mHasTransactionOpen = false;
        auto db = static_cast<Xapian::WritableDatabase*>(mDb);
        db->cancel_transaction();
    }
}

void FulltextIndex::remove(const QByteArray &key)
{
    Q_ASSERT(mDb);
    auto db = static_cast<Xapian::WritableDatabase*>(mDb);
    if (!mHasTransactionOpen) {
        db->begin_transaction();
        mHasTransactionOpen = true;
    }
    db->delete_document(idTerm(key));
}

QVector<QByteArray> FulltextIndex::lookup(const QString &searchTerm)
{
    Q_ASSERT(mDb);
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

