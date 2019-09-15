#include <QtTest>

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

        index.add("key1", "value1");
        index.add("key2", "value2");
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
        index.add("key3", "value3");
        QCOMPARE(index.lookup("value3").size(), 1);
        index.abortTransaction();
        QCOMPARE(index.lookup("value3").size(), 0);
    }
};

QTEST_MAIN(FulltextIndexTest)
#include "fulltextindextest.moc"
