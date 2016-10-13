#include <xapian.h>

#include "fulltextindex.h"

#include "log.h"
#include "definitions.h"
#include <QFile>

SINK_DEBUG_AREA("fulltextIndex")

FulltextIndex::FulltextIndex(const QByteArray &resourceInstanceIdentifier, const QByteArray &name)
    : mName(name)
{
    const auto dbPath = QFile::encodeName(Sink::resourceStorageLocation(resourceInstanceIdentifier) + '/' + name);
    try {
        mDb.reset(new Xapian::WritableDatabase(dbPath.toStdString(), Xapian::DB_CREATE_OR_OPEN));
    } catch (const Xapian::DatabaseError& e) {
        SinkError() << "Failed to open database" << dbPath << ":" << QString::fromStdString(e.get_msg());
    }
}

void FulltextIndex::add(const QByteArray &key, const QString &value)
{
    Xapian::TermGenerator generator;
    /* generator.set_stemmer(Xapian::Stem("en")); */

    Xapian::Document document;
    generator.set_document(document);

    generator.index_text(value.toStdString());
    /* generator.increase_termpos(1); */

    const auto idterm = "Q" + key.toStdString();
    document.add_boolean_term(idterm);
    mDb->replace_document(idterm, document);
}

void FulltextIndex::remove(const QByteArray &key)
{
    const auto idterm = "Q" + key.toStdString();
    mDb->delete_document(idterm);
}

/* void FulltextIndex::lookup(const QByteArray &key, const std::function<void(const QByteArray &value)> &resultHandler, const std::function<void(const Error &error)> &errorHandler, bool matchSubStringKeys) */
/* { */
    /* mDb.scan(key, */
    /*     [this, resultHandler](const QByteArray &key, const QByteArray &value) -> bool { */
    /*         resultHandler(value); */
    /*         return true; */
    /*     }, */
    /*     [this, errorHandler](const Sink::Storage::Error &error) { */
    /*         SinkWarning() << "Error while retrieving value" << error.message; */
    /*         errorHandler(Error(error.store, error.code, error.message)); */
    /*     }, */
    /*     matchSubStringKeys); */
/* } */

QByteArrayList FulltextIndex::lookup(const QString &searchTerm)
{
    QByteArrayList results;

    /* Xapian::Database db; */
    /* try { */
    /*     db = Xapian::Database(QFile::encodeName(dbPath).constData()); */
    /* } catch (const Xapian::DatabaseError& e) { */
    /*     SinkError() << "Failed to open database" << dbPath << ":" << QString::fromStdString(e.get_msg()); */
    /* } */
    try {
        /* const std::string term = QString::fromLatin1("C%1").arg(collectionId).toStdString(); */
        Xapian::QueryParser parser;
        auto query = parser.parse_query(searchTerm.toStdString());
        Xapian::Enquire enquire(*mDb);
        enquire.set_query(query);

        auto limit = 1000;
        Xapian::MSet mset = enquire.get_mset(0, limit);
        Xapian::MSetIterator it = mset.begin();
        for (;it != mset.end(); it++) {
            auto doc = it.get_document();
            const auto data = doc.get_data();
            QString id = QString::fromUtf8(data.c_str(), data.length());
            if (id.isEmpty()) {

            }
            results << id.toUtf8();
        }
    }
    catch (const Xapian::Error&) {
        // Nothing to do, move along
    }
    return results;
}

