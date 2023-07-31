#include <QTest>

#include <QString>

#include "definitions.h"
#include "storage.h"
#include "fulltextindex.h"

/**
 * Test of the index implementation
 */
class FulltextIndexTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        Sink::Storage::DataStore store(Sink::storageLocation(), "sink.dummy.instance1", Sink::Storage::DataStore::ReadWrite);
        store.removeFromDisk();
    }

    void cleanup()
    {
        Sink::Storage::DataStore store(Sink::storageLocation(), "sink.dummy.instance1", Sink::Storage::DataStore::ReadWrite);
        store.removeFromDisk();
    }

    void testIndex()
    {
        FulltextIndex index("sink.dummy.instance1", Sink::Storage::DataStore::ReadWrite);
        // qInfo() << QString("Found document 1 with terms: ") + index.getIndexContent(id1).terms.join(", ");
        // qInfo() << QString("Found document 2 with terms: ") + index.getIndexContent(id2).terms.join(", ");

        const auto key1 = Sink::Storage::Identifier::createIdentifier();
        const auto key2 = Sink::Storage::Identifier::createIdentifier();
        const auto key3 = Sink::Storage::Identifier::createIdentifier();

        index.add(key1, "value1");
        index.add(key2, "value2");
        index.commitTransaction();

        //Basic lookups
        QCOMPARE(index.lookup("value1").size(), 1);
        QCOMPARE(index.lookup("value1*").size(), 1);
        QCOMPARE(index.lookup("value").size(), 2);
        QCOMPARE(index.lookup("\"value1\"").size(), 1);
        QCOMPARE(index.lookup("\"value\"").size(), 0);
        QCOMPARE(index.lookup("value1 value2").size(), 0);
        QCOMPARE(index.lookup("value1 OR value2").size(), 2);

        //Rollback
        index.add(key3, "value3");
        QCOMPARE(index.lookup("value3").size(), 1);
        index.abortTransaction();
        QCOMPARE(index.lookup("value3").size(), 0);
    }

    void testIndexOrdering()
    {
        FulltextIndex index("sink.dummy.instance1", Sink::Storage::DataStore::ReadWrite);
        const auto key1 = Sink::Storage::Identifier::createIdentifier();
        const auto key2 = Sink::Storage::Identifier::createIdentifier();
        const auto key3 = Sink::Storage::Identifier::createIdentifier();

        const QDateTime dt{{2022,5,26},{9,38,0}};

        index.add(key1, "value1", dt.addDays(1));
        index.add(key2, "value2", dt);
        index.add(key3, "value3", dt.addDays(2));
        index.commitTransaction();
        const auto values = index.lookup("value");
        QCOMPARE(values.size(), 3);
        QCOMPARE(values[0], key3);
        QCOMPARE(values[1], key1);
        QCOMPARE(values[2], key2);
    }
};

QTEST_MAIN(FulltextIndexTest)
#include "fulltextindextest.moc"
