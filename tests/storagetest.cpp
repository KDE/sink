#include <QTest>

#include <iostream>

#include <QDebug>
#include <QString>
#include <QtConcurrent/QtConcurrentRun>

#include "common/storage.h"
#include "storage/key.h"

/**
 * Test of the storage implementation to ensure it can do the low level operations as expected.
 */
class StorageTest : public QObject
{
    Q_OBJECT
private:
    QString testDataPath;
    QByteArray dbName;
    const char *keyPrefix = "key";

    void populate(int count)
    {
        Sink::Storage::DataStore storage(testDataPath, {dbName, {{"default", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = storage.createTransaction(Sink::Storage::DataStore::ReadWrite);
        for (int i = 0; i < count; i++) {
            // This should perhaps become an implementation detail of the db?
            if (i % 10000 == 0) {
                if (i > 0) {
                    transaction.commit();
                    transaction = storage.createTransaction(Sink::Storage::DataStore::ReadWrite);
                }
            }
            transaction.openDatabase().write(keyPrefix + QByteArray::number(i), keyPrefix + QByteArray::number(i));
        }
        transaction.commit();
    }

    bool verify(Sink::Storage::DataStore &storage, int i)
    {
        bool success = true;
        bool keyMatch = true;
        const auto reference = keyPrefix + QByteArray::number(i);
        storage.createTransaction(Sink::Storage::DataStore::ReadOnly)
            .openDatabase()
            .scan(keyPrefix + QByteArray::number(i),
                [&keyMatch, &reference](const QByteArray &key, const QByteArray &value) -> bool {
                    if (value != reference) {
                        qDebug() << "Mismatch while reading";
                        keyMatch = false;
                    }
                    return keyMatch;
                },
                [&success](const Sink::Storage::DataStore::Error &error) {
                    qDebug() << error.message;
                    success = false;
                });
        return success && keyMatch;
    }

private slots:
    void initTestCase()
    {
        testDataPath = "./testdb";
        dbName = "test";
        Sink::Storage::DataStore{testDataPath, {dbName, {{"default", 0}}}}.removeFromDisk();
    }

    void cleanup()
    {
        Sink::Storage::DataStore{testDataPath, {dbName, {{"default", 0}}}}.removeFromDisk();
    }

    void testCleanup()
    {
        populate(1);
        Sink::Storage::DataStore{testDataPath, {dbName, {{"default", 0}}}}.removeFromDisk();
        QFileInfo info(testDataPath + "/" + dbName);
        QVERIFY(!info.exists());
    }

    void testRead()
    {
        const int count = 100;

        populate(count);

        // ensure we can read everything back correctly
        {
            Sink::Storage::DataStore storage(testDataPath, dbName);
            for (int i = 0; i < count; i++) {
                QVERIFY(verify(storage, i));
            }
        }
    }

    void testScan()
    {
        const int count = 100;
        populate(count);

        // ensure we can scan for values
        {
            int hit = 0;
            Sink::Storage::DataStore store(testDataPath, dbName);
            store.createTransaction(Sink::Storage::DataStore::ReadOnly)
                .openDatabase()
                .scan("", [&](const QByteArray &key, const QByteArray &value) -> bool {
                    if (key == "key50") {
                        hit++;
                    }
                    return true;
                });
            QCOMPARE(hit, 1);
        }

        // ensure we can read a single value
        {
            int hit = 0;
            bool foundInvalidValue = false;
            Sink::Storage::DataStore store(testDataPath, dbName);
            store.createTransaction(Sink::Storage::DataStore::ReadOnly)
                .openDatabase()
                .scan("key50", [&](const QByteArray &key, const QByteArray &value) -> bool {
                    if (key != "key50") {
                        foundInvalidValue = true;
                    }
                    hit++;
                    return true;
                });
            QVERIFY(!foundInvalidValue);
            QCOMPARE(hit, 1);
        }
    }

    void testNestedOperations()
    {
        populate(3);
        Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        transaction.openDatabase().scan("key1", [&](const QByteArray &key, const QByteArray &value) -> bool {
            transaction.openDatabase().remove(key, [](const Sink::Storage::DataStore::Error &) { QVERIFY(false); });
            return false;
        });
    }

    void testNestedTransactions()
    {
        populate(3);
        Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite);
        store.createTransaction(Sink::Storage::DataStore::ReadOnly)
            .openDatabase()
            .scan("key1", [&](const QByteArray &key, const QByteArray &value) -> bool {
                store.createTransaction(Sink::Storage::DataStore::ReadWrite).openDatabase().remove(key, [](const Sink::Storage::DataStore::Error &) { QVERIFY(false); });
                return false;
            });
    }

    void testReadEmptyDb()
    {
        bool gotResult = false;
        bool gotError = false;
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"default", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
        auto db = transaction.openDatabase("default", [&](const Sink::Storage::DataStore::Error &error) {
            qDebug() << error.message;
            gotError = true;
        });
        int numValues = db.scan("",
            [&](const QByteArray &key, const QByteArray &value) -> bool {
                gotResult = true;
                return false;
            },
            [&](const Sink::Storage::DataStore::Error &error) {
                qDebug() << error.message;
                gotError = true;
            });
        QCOMPARE(numValues, 0);
        QVERIFY(!gotResult);
        QVERIFY(!gotError);
    }

    void testConcurrentRead()
    {
        // With a count of 10000 this test is more likely to expose problems, but also takes some time to execute.
        const int count = 1000;

        populate(count);
        // QTest::qWait(500);

        // We repeat the test a bunch of times since failing is relatively random
        for (int tries = 0; tries < 10; tries++) {
            //clearEnv in combination with the bogus db layouts tests the dynamic named db opening as well.
            Sink::Storage::DataStore::clearEnv();
            bool error = false;
            // Try to concurrently read
            QList<QFuture<void>> futures;
            const int concurrencyLevel = 20;
            for (int num = 0; num < concurrencyLevel; num++) {
                futures << QtConcurrent::run([this, &error]() {
                    Sink::Storage::DataStore storage(testDataPath, {dbName, {{"bogus", 0}}}, Sink::Storage::DataStore::ReadOnly);
                    Sink::Storage::DataStore storage2(testDataPath, {dbName+ "2", {{"bogus", 0}}}, Sink::Storage::DataStore::ReadOnly);
                    for (int i = 0; i < count; i++) {
                        if (!verify(storage, i)) {
                            error = true;
                            break;
                        }
                    }
                });
            }
            for (auto future : futures) {
                future.waitForFinished();
            }
            QVERIFY(!error);
        }

        {
            Sink::Storage::DataStore(testDataPath, dbName).removeFromDisk();
            Sink::Storage::DataStore(testDataPath, dbName + "2").removeFromDisk();
        }
    }

    void testNoDuplicates()
    {
        bool gotResult = false;
        bool gotError = false;
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"default", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("default");
        db.write("key", "value");
        db.write("key", "value");

        int numValues = db.scan("",
            [&](const QByteArray &key, const QByteArray &value) -> bool {
                gotResult = true;
                return true;
            },
            [&](const Sink::Storage::DataStore::Error &error) {
                qDebug() << error.message;
                gotError = true;
            });

        QCOMPARE(numValues, 1);
        QVERIFY(!gotError);
        QVERIFY(gotResult);
    }

    void testDuplicates()
    {
        bool gotResult = false;
        bool gotError = false;
        const int flags = Sink::Storage::AllowDuplicates;
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"default", flags}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("default", nullptr, flags);
        db.write("key", "value1");
        db.write("key", "value2");
        int numValues = db.scan("key",
            [&](const QByteArray &key, const QByteArray &value) -> bool {
                gotResult = true;
                return true;
            },
            [&](const Sink::Storage::DataStore::Error &error) {
                qDebug() << error.message;
                gotError = true;
            });

        QCOMPARE(numValues, 2);
        QVERIFY(!gotError);
    }

    void testNonexitingNamedDb()
    {
        bool gotResult = false;
        bool gotError = false;
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadOnly);
        QVERIFY(!store.exists());
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
        Sink::Storage::DataStore::getUids("test", transaction, [&](const auto &uid) {});
        int numValues = transaction
                            .openDatabase("test")
                            .scan("",
                                [&](const QByteArray &key, const QByteArray &value) -> bool {
                                    gotResult = true;
                                    return false;
                                },
                                [&](const Sink::Storage::DataStore::Error &error) {
                                    qDebug() << error.message;
                                    gotError = true;
                                });
        QCOMPARE(numValues, 0);
        QVERIFY(!gotResult);
        QVERIFY(!gotError);
    }

    /*
     * This scenario tests a very specific pattern that can appear with new named databases.
     * * A read-only transaction is opened
     * * A write-transaction creates a new named db.
     * * We try to access that named-db from the already open transaction.
     */
    void testNewDbInOpenTransaction()
    {
        //Create env, otherwise we don't even get a transaction
        {
            Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite);
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        }
        //Open a longlived transaction
        Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadOnly);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);

        //Create the named database
        {
            Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite);
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
            transaction.openDatabase("test");
            transaction.commit();
        }


        //Try to access the named database in the existing transaction. Opening should fail.
        bool gotResult = false;
        bool gotError = false;
        int numValues = transaction
                            .openDatabase("test")
                            .scan("",
                                [&](const QByteArray &key, const QByteArray &value) -> bool {
                                    gotResult = true;
                                    return false;
                                },
                                [&](const Sink::Storage::DataStore::Error &error) {
                                    qDebug() << error.message;
                                    gotError = true;
                                });
        QCOMPARE(numValues, 0);
        QVERIFY(!gotResult);
        QVERIFY(!gotError);
    }

    void testWriteToNamedDb()
    {
        bool gotError = false;
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        store.createTransaction(Sink::Storage::DataStore::ReadWrite)
            .openDatabase("test")
            .write("key1", "value1", [&](const Sink::Storage::DataStore::Error &error) {
                qDebug() << error.message;
                gotError = true;
            });
        QVERIFY(!gotError);
    }

    void testWriteDuplicatesToNamedDb()
    {
        bool gotError = false;

        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        store.createTransaction(Sink::Storage::DataStore::ReadWrite)
            .openDatabase("test", nullptr, Sink::Storage::AllowDuplicates)
            .write("key1", "value1", [&](const Sink::Storage::DataStore::Error &error) {
                qDebug() << error.message;
                gotError = true;
            });
        QVERIFY(!gotError);
    }

    // By default we want only exact matches
    void testSubstringKeys()
    {
        const int flags = Sink::Storage::AllowDuplicates;
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", flags}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test", nullptr, flags);
        db.write("sub", "value1");
        db.write("subsub", "value2");
        int numValues = db.scan("sub", [&](const QByteArray &key, const QByteArray &value) -> bool { return true; });

        QCOMPARE(numValues, 1);
    }

    void testFindSubstringKeys()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test");
        db.write("sub", "value1");
        db.write("subsub", "value2");
        db.write("wubsub", "value3");
        int numValues = db.scan("sub", [&](const QByteArray &key, const QByteArray &value) -> bool { return true; }, nullptr, true);

        QCOMPARE(numValues, 2);
    }

    void testFindSubstringKeysWithDuplicatesEnabled()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test", nullptr, Sink::Storage::AllowDuplicates);
        db.write("sub", "value1");
        db.write("subsub", "value2");
        db.write("wubsub", "value3");
        int numValues = db.scan("sub", [&](const QByteArray &key, const QByteArray &value) -> bool { return true; }, nullptr, true);

        QCOMPARE(numValues, 2);
    }

    void testKeySorting()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test");
        db.write("sub_2", "value2");
        db.write("sub_1", "value1");
        db.write("sub_3", "value3");
        QList<QByteArray> results;
        int numValues = db.scan("sub", [&](const QByteArray &key, const QByteArray &value) -> bool {
            results << value;
            return true;
        }, nullptr, true);

        QCOMPARE(numValues, 3);
        QCOMPARE(results.at(0), QByteArray("value1"));
        QCOMPARE(results.at(1), QByteArray("value2"));
        QCOMPARE(results.at(2), QByteArray("value3"));
    }

    // Ensure we don't retrieve a key that is greater than the current key. We only want equal keys.
    void testKeyRange()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test", nullptr, Sink::Storage::AllowDuplicates);
        db.write("sub1", "value1");
        int numValues = db.scan("sub", [&](const QByteArray &key, const QByteArray &value) -> bool { return true; });

        QCOMPARE(numValues, 0);
    }

    void testFindLatest()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test");
        db.write("sub1", "value1");
        db.write("sub2", "value2");
        db.write("wub3", "value3");
        db.write("wub4", "value4");
        QByteArray result;
        db.findLatest("sub", [&](const QByteArray &key, const QByteArray &value) { result = value; });

        QCOMPARE(result, QByteArray("value2"));
    }

    void testFindLatestInSingle()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test");
        db.write("sub2", "value2");
        QByteArray result;
        db.findLatest("sub", [&](const QByteArray &key, const QByteArray &value) { result = value; });

        QCOMPARE(result, QByteArray("value2"));
    }

    void testFindLast()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test");
        db.write("sub2", "value2");
        db.write("wub3", "value3");
        QByteArray result;
        db.findLatest("wub", [&](const QByteArray &key, const QByteArray &value) { result = value; });

        QCOMPARE(result, QByteArray("value3"));
    }

    void testRecordRevision()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, Sink::Storage::DataStore::baseDbs()}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto id = Sink::Storage::Identifier::fromDisplayByteArray("{c5d06a9f-1534-4c52-b8ea-415db68bdadf}");
        auto id2 = Sink::Storage::Identifier::fromDisplayByteArray("{c5d06a9f-1534-4c52-b8ea-415db68bdad2}");
        auto id3 = Sink::Storage::Identifier::fromDisplayByteArray("{18a72a62-f8f7-4bc1-a087-ec25f143f60b}");
        Sink::Storage::DataStore::recordRevision(transaction, 1, id, "type");
        Sink::Storage::DataStore::recordRevision(transaction, 2, id2, "type");
        Sink::Storage::DataStore::recordRevision(transaction, 3, id3, "type");

        QCOMPARE(Sink::Storage::DataStore::getTypeFromRevision(transaction, 1), QByteArray("type"));
        QCOMPARE(Sink::Storage::DataStore::getUidFromRevision(transaction, 1).toDisplayByteArray(), id.toDisplayByteArray());
        QCOMPARE(Sink::Storage::DataStore::getLatestRevisionFromUid(transaction, id), 1);
        QCOMPARE(Sink::Storage::DataStore::getLatestRevisionFromUid(transaction, id2), 2);
        QCOMPARE(Sink::Storage::DataStore::getLatestRevisionFromUid(transaction, id3), 3);

        Sink::Storage::DataStore::recordRevision(transaction, 10, id, "type");
        QCOMPARE(Sink::Storage::DataStore::getLatestRevisionFromUid(transaction, id), 10);

        QCOMPARE(Sink::Storage::DataStore::getRevisionsUntilFromUid(transaction, id, 10).size(), 1);
        QCOMPARE(Sink::Storage::DataStore::getRevisionsUntilFromUid(transaction, id, 11).size(), 2);
        QCOMPARE(Sink::Storage::DataStore::getRevisionsFromUid(transaction, id).size(), 2);
    }

    void testRecordRevisionSorting()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        QByteArray result;
        auto db = transaction.openDatabase("test");
        const auto uid = "{c5d06a9f-1534-4c52-b8ea-415db68bdadf}";
        //Ensure we can sort 1 and 10 properly (by default string comparison 10 comes before 6)
        const auto id = Sink::Storage::Identifier::fromDisplayByteArray(uid);
        auto key = Sink::Storage::Key(id, 6);
        db.write(key.toInternalByteArray(), "value1");
        key.setRevision(10);
        db.write(key.toInternalByteArray(), "value2");
        db.findLatest(id.toInternalByteArray(), [&](const QByteArray &key, const QByteArray &value) { result = value; });
        QCOMPARE(result, QByteArray("value2"));
    }

    void testRecordRevisionRandom()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, Sink::Storage::DataStore::baseDbs()}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);

        for (auto i = 1; i <= 500; i++) {
            const auto uid = Sink::Storage::DataStore::generateUid();
            const auto id = Sink::Storage::Identifier::fromDisplayByteArray(uid);
            Sink::Storage::DataStore::recordRevision(transaction, i, id, "type");

            QCOMPARE(Sink::Storage::DataStore::getTypeFromRevision(transaction, i), QByteArray("type"));
            QCOMPARE(Sink::Storage::DataStore::getUidFromRevision(transaction, i).toDisplayByteArray(), id.toDisplayByteArray());
            QCOMPARE(Sink::Storage::DataStore::getLatestRevisionFromUid(transaction, id), i);
        }
    }

    void setupTestFindRange(Sink::Storage::DataStore::NamedDatabase &db)
    {
        db.write("0002", "value1");
        db.write("0003", "value2");
        db.write("0004", "value3");
        db.write("0005", "value4");
    }

    void testFindRangeOptimistic()
    {
        Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test");
        setupTestFindRange(db);
        QByteArrayList results;
        db.findAllInRange("0002", "0004", [&](const QByteArray &key, const QByteArray &value) { results << value; });

        QCOMPARE(results, (QByteArrayList{"value1", "value2", "value3"}));
    }

    void testFindRangeNothing()
    {
        Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test");
        setupTestFindRange(db);

        QByteArrayList results1;
        db.findAllInRange("0000", "0001", [&](const QByteArray &key, const QByteArray &value) { results1 << value; });
        QCOMPARE(results1, QByteArrayList{});

        QByteArrayList results2;
        db.findAllInRange("0000", "0000", [&](const QByteArray &key, const QByteArray &value) { results2 << value; });
        QCOMPARE(results2, QByteArrayList{});

        QByteArrayList results3;
        db.findAllInRange("0006", "0010", [&](const QByteArray &key, const QByteArray &value) { results3 << value; });
        QCOMPARE(results3, QByteArrayList{});

        QByteArrayList results4;
        db.findAllInRange("0010", "0010", [&](const QByteArray &key, const QByteArray &value) { results4 << value; });
        QCOMPARE(results4, QByteArrayList{});
    }

    void testFindRangeSingle()
    {
        Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test");
        setupTestFindRange(db);

        QByteArrayList results1;
        db.findAllInRange("0004", "0004", [&](const QByteArray &key, const QByteArray &value) { results1 << value; });
        QCOMPARE(results1, QByteArrayList{"value3"});
    }

    void testFindRangeOutofBounds()
    {
        Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test");
        setupTestFindRange(db);

        QByteArrayList results1;
        db.findAllInRange("0000", "0010", [&](const QByteArray &key, const QByteArray &value) { results1 << value; });
        QCOMPARE(results1, (QByteArrayList{"value1", "value2", "value3", "value4"}));

        QByteArrayList results2;
        db.findAllInRange("0003", "0010", [&](const QByteArray &key, const QByteArray &value) { results2 << value; });
        QCOMPARE(results2, (QByteArrayList{"value2", "value3", "value4"}));

        QByteArrayList results3;
        db.findAllInRange("0000", "0003", [&](const QByteArray &key, const QByteArray &value) { results3 << value; });
        QCOMPARE(results3, (QByteArrayList{"value1", "value2"}));
    }

    void testTransactionVisibility()
    {
        auto readValue = [](const Sink::Storage::DataStore::NamedDatabase &db, const QByteArray) {
            QByteArray result;
            db.scan("key1", [&](const QByteArray &, const QByteArray &value) {
                result = value;
                return true;
            });
            return result;
        };
        {
            Sink::Storage::DataStore store(testDataPath, {dbName, {{"testTransactionVisibility", 0}}}, Sink::Storage::DataStore::ReadWrite);
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);

            auto db = transaction.openDatabase("testTransactionVisibility");
            db.write("key1", "foo");
            QCOMPARE(readValue(db, "key1"), QByteArray("foo"));

            {
                auto transaction2 = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
                auto db2 = transaction2
                    .openDatabase("testTransactionVisibility");
                QCOMPARE(readValue(db2, "key1"), QByteArray());
            }
            transaction.commit();
            {
                auto transaction2 = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
                auto db2 = transaction2
                    .openDatabase("testTransactionVisibility");
                QCOMPARE(readValue(db2, "key1"), QByteArray("foo"));
            }

        }
    }

    void testCopyTransaction()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"a", 0}, {"b", 0}, {"c", 0}}}, Sink::Storage::DataStore::ReadWrite);
        {
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
            transaction.openDatabase("a");
            transaction.openDatabase("b");
            transaction.openDatabase("c");
            transaction.commit();
        }
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
        for (int i = 0; i < 1000; i++) {
            transaction.openDatabase("a");
            transaction.openDatabase("b");
            transaction.openDatabase("c");
            transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
        }
    }

    /*
     * This test is meant to find problems with the multi-process architecture and initial database creation.
     * If we create named databases dynamically (not all up front), it is possilbe that we violate the rule
     * that mdb_open_dbi may only be used by a single thread at a time.
     * This test is meant to stress that condition.
     *
     * FIXME this test ends up locking up every now and then (don't know why).
     * All reader threads get stuck on the "QMutexLocker createDbiLocker(&sCreateDbiLock);" mutex in openDatabase,
     * and the writer probably crashed. The testfunction then times out.
     * I can't reliably reproduce it and thus fix it, so the test remains disabled for now.
     */
    //void testReadDuringExternalProcessWrite()
    //{

    //    QList<QFuture<void>> futures;
    //    for (int i = 0; i < 5; i++) {
    //        futures <<  QtConcurrent::run([&]() {
    //            QTRY_VERIFY(Sink::Storage::DataStore(testDataPath, dbName, Sink::Storage::DataStore::ReadOnly).exists());
    //            Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadOnly);
    //            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
    //            for (int i = 0; i < 100000; i++) {
    //                transaction.openDatabase("a");
    //                transaction.openDatabase("b");
    //                transaction.openDatabase("c");
    //                transaction.openDatabase("p");
    //                transaction.openDatabase("q");
    //            }
    //        });
    //    }

    //    //Start writing to the db from a separate process
    //    QVERIFY(QProcess::startDetached(QCoreApplication::applicationDirPath() + "/dbwriter", QStringList() << testDataPath << dbName << QString::number(100000)));

    //    for (auto future : futures) {
    //        future.waitForFinished();
    //    }

    //}

    void testRecordUid()
    {

        QMap<QByteArray, int> dbs = {
                {"revisionType", 0},
                {"revisions", 0},
                {"uids", 0},
                {"default", 0},
                {"__flagtable", 0},
                {"typeuids", 0},
                {"type2uids", 0}
            };

        Sink::Storage::DataStore store(testDataPath, {dbName, dbs}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto id1 = Sink::Storage::Identifier::fromDisplayByteArray("{c5d06a9f-1534-4c52-b8ea-415db68bdad1}");
        auto id2 = Sink::Storage::Identifier::fromDisplayByteArray("{c5d06a9f-1534-4c52-b8ea-415db68bdad2}");
        auto id3 = Sink::Storage::Identifier::fromDisplayByteArray("{c5d06a9f-1534-4c52-b8ea-415db68bdad3}");
        Sink::Storage::DataStore::recordUid(transaction, id1, "type");
        Sink::Storage::DataStore::recordUid(transaction, id2, "type");
        Sink::Storage::DataStore::recordUid(transaction, id3, "type2");

        {
            QVector<QByteArray> uids;
            Sink::Storage::DataStore::getUids("type", transaction, [&](const Sink::Storage::Identifier &r) {
                uids << r.toDisplayByteArray();
            });
            QVector<QByteArray> expected{id1.toDisplayByteArray(), id2.toDisplayByteArray()};
            QCOMPARE(uids, expected);
        }

        Sink::Storage::DataStore::removeUid(transaction, id2, "type");

        {
            QVector<QByteArray> uids;
            Sink::Storage::DataStore::getUids("type", transaction, [&](const Sink::Storage::Identifier &r) {
                uids << r.toDisplayByteArray();
            });
            QVector<QByteArray> expected{{id1.toDisplayByteArray()}};
            QCOMPARE(uids, expected);
        }
    }

    void testDbiVisibility()
    {
        auto readValue = [](const Sink::Storage::DataStore::NamedDatabase &db, const QByteArray) {
            QByteArray result;
            db.scan("key1", [&](const QByteArray &, const QByteArray &value) {
                result = value;
                return true;
            });
            return result;
        };
        {
            Sink::Storage::DataStore store(testDataPath, {dbName, {{"testTransactionVisibility", 0}}}, Sink::Storage::DataStore::ReadWrite);
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);

            auto db = transaction.openDatabase("testTransactionVisibility");
            db.write("key1", "foo");
            QCOMPARE(readValue(db, "key1"), QByteArray("foo"));
            transaction.commit();
        }
        Sink::Storage::DataStore::clearEnv();

        //Try to read-only dynamic opening of the db.
        //This is the case if we don't have all databases available upon initializatoin and we don't (e.g. because the db hasn't been created yet)
        {
            // Trick the db into not loading all dbs by passing in a bogus layout.
            Sink::Storage::DataStore store(testDataPath, {dbName, {{"bogus", 0}}}, Sink::Storage::DataStore::ReadOnly);

            //This transaction should open the dbi
            auto transaction2 = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
            auto db2 = transaction2.openDatabase("testTransactionVisibility");
            QCOMPARE(readValue(db2, "key1"), QByteArray("foo"));

            //This transaction should have the dbi available
            auto transaction3 = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
            auto db3 = transaction3.openDatabase("testTransactionVisibility");
            QCOMPARE(readValue(db3, "key1"), QByteArray("foo"));
        }

        Sink::Storage::DataStore::clearEnv();
        //Try to read-write dynamic opening of the db.
        //This is the case if we don't have all databases available upon initialization and we don't (e.g. because the db hasn't been created yet)
        {
            // Trick the db into not loading all dbs by passing in a bogus layout.
            Sink::Storage::DataStore store(testDataPath, {dbName, {{"bogus", 0}}}, Sink::Storage::DataStore::ReadWrite);

            //This transaction should open the dbi
            auto transaction2 = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
            auto db2 = transaction2.openDatabase("testTransactionVisibility");
            QCOMPARE(readValue(db2, "key1"), QByteArray("foo"));

            //This transaction should have the dbi available (creating two write transactions obviously doesn't work)
            //NOTE: we don't support this scenario. A write transaction must commit or abort before a read transaction opens the same database.
            // auto transaction3 = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
            // auto db3 = transaction3.openDatabase("testTransactionVisibility");
            // QCOMPARE(readValue(db3, "key1"), QByteArray("foo"));

            //Ensure we can still open further dbis in the write transaction
            auto db4 = transaction2.openDatabase("anotherDb");
        }

    }

    void testIntegerKeys()
    {
        const int flags = Sink::Storage::IntegerKeys;
        Sink::Storage::DataStore store(testDataPath,
            { dbName, { { "test", flags } } }, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("testIntegerKeys", {}, flags);
        db.write(0, "value1");
        db.write(1, "value2");

        size_t resultKey;
        QByteArray result;
        int numValues = db.scan(0, [&](size_t key, const QByteArray &value) -> bool {
            resultKey = key;
            result = value;
            return true;
        });

        QCOMPARE(numValues, 1);
        QCOMPARE(resultKey, size_t{0});
        QCOMPARE(result, QByteArray{"value1"});

        int numValues2 = db.scan(1, [&](size_t key, const QByteArray &value) -> bool {
            resultKey = key;
            result = value;
            return true;
        });

        QCOMPARE(numValues2, 1);
        QCOMPARE(resultKey, size_t{1});
        QCOMPARE(result, QByteArray{"value2"});
    }

    void testDuplicateIntegerKeys()
    {
        const int flags = Sink::Storage::IntegerKeys | Sink::Storage::AllowDuplicates;
        Sink::Storage::DataStore store(testDataPath,
            { dbName, { { "testDuplicateIntegerKeys", flags} } },
            Sink::Storage::DataStore::ReadWrite);
        {
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
            auto db = transaction.openDatabase("testDuplicateIntegerKeys", {}, flags);
            db.write(0, "value1");
            db.write(1, "value2");
            db.write(1, "value3");
            QSet<QByteArray> results;
            int numValues = db.scan(1, [&](size_t, const QByteArray &value) -> bool {
                results << value;
                return true;
            });

            QCOMPARE(numValues, 2);
            QCOMPARE(results.size(), 2);
            QVERIFY(results.contains("value2"));
            QVERIFY(results.contains("value3"));
        }

        //Test full scan over keys
        {
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
            auto db = transaction.openDatabase("testDuplicateIntegerKeys", {}, flags);
            bool success = true;
            QSet<QByteArray> results;
            db.scan({}, [&](const QByteArray &key, const QByteArray &value) {
                results << value;
                return true;
            },
            [&success](const Sink::Storage::DataStore::Error &error) {
                qWarning() << error.message;
                success = false;
            }, true);
            QVERIFY(success);
            QCOMPARE(results.size(), 3);
        }

        //Test find last
        {
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
            auto db = transaction.openDatabase("testDuplicateIntegerKeys", {}, flags);
            bool success = false;
            db.findLast(Sink::sizeTToByteArray(1), [&](const QByteArray &key, const QByteArray &value) {
                success = true;
            },
            [&success](const Sink::Storage::DataStore::Error &error) {
                qDebug() << error.message;
                success = false;
            });
            QVERIFY(success);
        }
    }

    void testDuplicateWithIntegerValues()
    {
        const int flags = Sink::Storage::AllowDuplicates | Sink::Storage::IntegerValues;
        Sink::Storage::DataStore store(testDataPath,
            { dbName, { { "testDuplicateWithIntegerValues", flags} } },
            Sink::Storage::DataStore::ReadWrite);

        const size_t number1 = 1;
        const size_t number2 = 2;

        const QByteArray number1BA = Sink::sizeTToByteArray(number1);
        const QByteArray number2BA = Sink::sizeTToByteArray(number2);
        {
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
            auto db = transaction.openDatabase("testDuplicateWithIntegerValues", {}, flags);

            db.write(0, number1BA);
            db.write(1, number2BA);
            db.write(1, number1BA);

            QList<QByteArray> results;
            int numValues = db.scan(1, [&](size_t, const QByteArray &value) -> bool {
                results << value;
                return true;
            });

            QCOMPARE(numValues, 2);
            QCOMPARE(results.size(), 2);
            QCOMPARE(results[0], number1BA);
            QCOMPARE(results[1], number2BA);
        }

        //Test find last
        {
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
            auto db = transaction.openDatabase("testDuplicateWithIntegerValues", {}, flags);
            bool success = false;
            QByteArray result;
            db.findLast(Sink::sizeTToByteArray(1), [&](const QByteArray &key, const QByteArray &value) {
                result = value;
                success = true;
            },
            [&success](const Sink::Storage::DataStore::Error &error) {
                qDebug() << error.message;
                success = false;
            });
            QVERIFY(success);
            QCOMPARE(result, number2BA);
        }
    }

    void testIntegerKeyMultipleOf256()
    {
        const int flags = Sink::Storage::IntegerKeys;
        Sink::Storage::DataStore store(testDataPath,
                { dbName, { {"testIntegerKeyMultipleOf256", flags} } },
                Sink::Storage::DataStore::ReadWrite);

        {
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
            auto db = transaction.openDatabase("testIntegerKeyMultipleOf256", {}, flags);

            db.write(0x100, "hello");
            db.write(0x200, "hello2");
            db.write(0x42, "hello3");

            transaction.commit();
        }

        {
            auto transaction2 = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
            auto db = transaction2.openDatabase("testIntegerKeyMultipleOf256", {}, flags);

            size_t resultKey;
            QByteArray resultValue;
            db.scan(0x100, [&] (size_t key, const QByteArray &value) {
                resultKey = key;
                resultValue = value;
                return false;
            });

            QCOMPARE(resultKey, size_t{0x100});
            QCOMPARE(resultValue, QByteArray{"hello"});
        }
    }

    void testIntegerProperlySorted()
    {
        const int flags = Sink::Storage::IntegerKeys;
        Sink::Storage::DataStore store(testDataPath,
                { dbName, { {"testIntegerProperlySorted", flags} } },
                Sink::Storage::DataStore::ReadWrite);

        {
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
            auto db = transaction.openDatabase("testIntegerProperlySorted", {}, flags);

            for (size_t i = 0; i < 0x100; ++i) {
                db.write(i, "hello");
            }

            size_t previous = 0;
            bool success = true;
            db.scan("", [&] (const QByteArray &key, const QByteArray &value) {
                size_t current = Sink::byteArrayToSizeT(key);
                if (current < previous) {
                    success = false;
                    return false;
                }

                previous = current;
                return true;
            });

            QVERIFY2(success, "Integer are not properly sorted before commit");

            transaction.commit();
        }

        {
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
            auto db = transaction.openDatabase("testIntegerProperlySorted", {}, flags);

            size_t previous = 0;
            bool success = true;
            db.scan("", [&] (const QByteArray &key, const QByteArray &value) {
                size_t current = Sink::byteArrayToSizeT(key);
                if (current < previous) {
                    success = false;
                    return false;
                }

                previous = current;
                return true;
            });

            QVERIFY2(success, "Integer are not properly sorted after commit");
        }
    }

    /**
     * Demonstrate how long running transactions result in an accumulation of free-pages.
     */
    void testFreePages()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);

        // With any ro transaction ongoing we just accumulate endless free pages
        // auto rotransaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
        for (int i = 0; i < 5; i++) {
            {
                auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
                transaction.openDatabase("test").write("sub" + QByteArray::number(i), "value1");
            }
            // If we reset the rotransaction the accumulation is not a problem (because previous free pages can be reused at that point)
            // auto rotransaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
            {
                auto stat = store.createTransaction(Sink::Storage::DataStore::ReadOnly).stat(false);
                QVERIFY(stat.freePages <= 6);
            }
        }
    }
};

QTEST_MAIN(StorageTest)
#include "storagetest.moc"
