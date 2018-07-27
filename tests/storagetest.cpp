#include <QtTest>

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
            bool error = false;
            // Try to concurrently read
            QList<QFuture<void>> futures;
            const int concurrencyLevel = 20;
            for (int num = 0; num < concurrencyLevel; num++) {
                futures << QtConcurrent::run([this, &error]() {
                    Sink::Storage::DataStore storage(testDataPath, dbName, Sink::Storage::DataStore::ReadOnly);
                    Sink::Storage::DataStore storage2(testDataPath, dbName + "2", Sink::Storage::DataStore::ReadOnly);
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
        auto db = transaction.openDatabase("default", nullptr, false);
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
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"default", 0x04}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("default", nullptr, true);
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
        Sink::Storage::DataStore::getUids("test", transaction, [&](const QByteArray &uid) {});
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
            .openDatabase("test", nullptr, true)
            .write("key1", "value1", [&](const Sink::Storage::DataStore::Error &error) {
                qDebug() << error.message;
                gotError = true;
            });
        QVERIFY(!gotError);
    }

    // By default we want only exact matches
    void testSubstringKeys()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0x04}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test", nullptr, true);
        db.write("sub", "value1");
        db.write("subsub", "value2");
        int numValues = db.scan("sub", [&](const QByteArray &key, const QByteArray &value) -> bool { return true; });

        QCOMPARE(numValues, 1);
    }

    void testFindSubstringKeys()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test", nullptr, false);
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
        auto db = transaction.openDatabase("test", nullptr, true);
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
        auto db = transaction.openDatabase("test", nullptr, false);
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
        auto db = transaction.openDatabase("test", nullptr, true);
        db.write("sub1", "value1");
        int numValues = db.scan("sub", [&](const QByteArray &key, const QByteArray &value) -> bool { return true; });

        QCOMPARE(numValues, 0);
    }

    void testFindLatest()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test", nullptr, false);
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
        auto db = transaction.openDatabase("test", nullptr, false);
        db.write("sub2", "value2");
        QByteArray result;
        db.findLatest("sub", [&](const QByteArray &key, const QByteArray &value) { result = value; });

        QCOMPARE(result, QByteArray("value2"));
    }

    void testFindLast()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test", nullptr, false);
        db.write("sub2", "value2");
        db.write("wub3", "value3");
        QByteArray result;
        db.findLatest("wub", [&](const QByteArray &key, const QByteArray &value) { result = value; });

        QCOMPARE(result, QByteArray("value3"));
    }

    static QMap<QByteArray, int> baseDbs()
    {
        return {{"revisionType", 0},
                {"revisions", 0},
                {"uids", 0},
                {"default", 0},
                {"__flagtable", 0}};
    }

    void testRecordRevision()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, baseDbs()}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        Sink::Storage::DataStore::recordRevision(transaction, 1, "uid", "type");
        QCOMPARE(Sink::Storage::DataStore::getTypeFromRevision(transaction, 1), QByteArray("type"));
        QCOMPARE(Sink::Storage::DataStore::getUidFromRevision(transaction, 1), QByteArray("uid"));
    }

    void testRecordRevisionSorting()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"test", 0}}}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        QByteArray result;
        auto db = transaction.openDatabase("test", nullptr, false);
        const auto uid = "{c5d06a9f-1534-4c52-b8ea-415db68bdadf}";
        //Ensure we can sort 1 and 10 properly (by default string comparison 10 comes before 6)
        const auto id = Sink::Storage::Identifier::fromDisplayByteArray(uid);
        auto key = Sink::Storage::Key(id, 6);
        db.write(key.toInternalByteArray(), "value1");
        key.setRevision(10);
        db.write(key.toInternalByteArray(), "value2");
        db.findLatest(uid, [&](const QByteArray &key, const QByteArray &value) { result = value; });
        QCOMPARE(result, QByteArray("value2"));
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
        auto db = transaction.openDatabase("test", nullptr, false);
        setupTestFindRange(db);
        QByteArrayList results;
        db.findAllInRange("0002", "0004", [&](const QByteArray &key, const QByteArray &value) { results << value; });

        QCOMPARE(results, (QByteArrayList{"value1", "value2", "value3"}));
    }

    void testFindRangeNothing()
    {
        Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test", nullptr, false);
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
        auto db = transaction.openDatabase("test", nullptr, false);
        setupTestFindRange(db);

        QByteArrayList results1;
        db.findAllInRange("0004", "0004", [&](const QByteArray &key, const QByteArray &value) { results1 << value; });
        QCOMPARE(results1, QByteArrayList{"value3"});
    }

    void testFindRangeOutofBounds()
    {
        Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase("test", nullptr, false);
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

            auto db = transaction.openDatabase("testTransactionVisibility", nullptr, false);
            db.write("key1", "foo");
            QCOMPARE(readValue(db, "key1"), QByteArray("foo"));

            {
                auto transaction2 = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
                auto db2 = transaction2
                    .openDatabase("testTransactionVisibility", nullptr, false);
                QCOMPARE(readValue(db2, "key1"), QByteArray());
            }
            transaction.commit();
            {
                auto transaction2 = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
                auto db2 = transaction2
                    .openDatabase("testTransactionVisibility", nullptr, false);
                QCOMPARE(readValue(db2, "key1"), QByteArray("foo"));
            }

        }
    }

    void testCopyTransaction()
    {
        Sink::Storage::DataStore store(testDataPath, {dbName, {{"a", 0}, {"b", 0}, {"c", 0}}}, Sink::Storage::DataStore::ReadWrite);
        {
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
            transaction.openDatabase("a", nullptr, false);
            transaction.openDatabase("b", nullptr, false);
            transaction.openDatabase("c", nullptr, false);
            transaction.commit();
        }
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
        for (int i = 0; i < 1000; i++) {
            transaction.openDatabase("a", nullptr, false);
            transaction.openDatabase("b", nullptr, false);
            transaction.openDatabase("c", nullptr, false);
            transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
        }
    }

    /*
     * This test is meant to find problems with the multi-process architecture and initial database creation.
     * If we create named databases dynamically (not all up front), it is possilbe that we violate the rule
     * that mdb_open_dbi may only be used by a single thread at a time.
     * This test is meant to stress that condition.
     *
     * However, it yields absolutely nothing.
     */
    void testReadDuringExternalProcessWrite()
    {

        QList<QFuture<void>> futures;
        for (int i = 0; i < 5; i++) {
            futures <<  QtConcurrent::run([&]() {
                QTRY_VERIFY(Sink::Storage::DataStore(testDataPath, dbName, Sink::Storage::DataStore::ReadOnly).exists());
                Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadOnly);
                auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
                for (int i = 0; i < 100000; i++) {
                    transaction.openDatabase("a", nullptr, false);
                    transaction.openDatabase("b", nullptr, false);
                    transaction.openDatabase("c", nullptr, false);
                    transaction.openDatabase("p", nullptr, false);
                    transaction.openDatabase("q", nullptr, false);
                }
            });
        }

        //Start writing to the db from a separate process
        QVERIFY(QProcess::startDetached(QCoreApplication::applicationDirPath() + "/dbwriter", QStringList() << testDataPath << dbName << QString::number(100000)));

        for (auto future : futures) {
            future.waitForFinished();
        }

    }

    void testRecordUid()
    {

        QMap<QByteArray, int> dbs = {{"revisionType", 0},
                {"revisions", 0},
                {"uids", 0},
                {"default", 0},
                {"__flagtable", 0},
                {"typeuids", 0},
                {"type2uids", 0}
            };

        Sink::Storage::DataStore store(testDataPath, {dbName, dbs}, Sink::Storage::DataStore::ReadWrite);
        auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        Sink::Storage::DataStore::recordUid(transaction, "uid1", "type");
        Sink::Storage::DataStore::recordUid(transaction, "uid2", "type");
        Sink::Storage::DataStore::recordUid(transaction, "uid3", "type2");

        {
            QVector<QByteArray> uids;
            Sink::Storage::DataStore::getUids("type", transaction, [&](const QByteArray &r) {
                uids << r;
            });
            QVector<QByteArray> expected{{"uid1"}, {"uid2"}};
            QCOMPARE(uids, expected);
        }

        Sink::Storage::DataStore::removeUid(transaction, "uid2", "type");

        {
            QVector<QByteArray> uids;
            Sink::Storage::DataStore::getUids("type", transaction, [&](const QByteArray &r) {
                uids << r;
            });
            QVector<QByteArray> expected{{"uid1"}};
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

            auto db = transaction.openDatabase("testTransactionVisibility", nullptr, false);
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
            auto db2 = transaction2.openDatabase("testTransactionVisibility", nullptr, false);
            QCOMPARE(readValue(db2, "key1"), QByteArray("foo"));

            //This transaction should have the dbi available
            auto transaction3 = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
            auto db3 = transaction3.openDatabase("testTransactionVisibility", nullptr, false);
            QCOMPARE(readValue(db3, "key1"), QByteArray("foo"));
        }

        Sink::Storage::DataStore::clearEnv();
        //Try to read-write dynamic opening of the db.
        //This is the case if we don't have all databases available upon initializatoin and we don't (e.g. because the db hasn't been created yet)
        {
            // Trick the db into not loading all dbs by passing in a bogus layout.
            Sink::Storage::DataStore store(testDataPath, {dbName, {{"bogus", 0}}}, Sink::Storage::DataStore::ReadWrite);

            //This transaction should open the dbi
            auto transaction2 = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
            auto db2 = transaction2.openDatabase("testTransactionVisibility", nullptr, false);
            QCOMPARE(readValue(db2, "key1"), QByteArray("foo"));

            //This transaction should have the dbi available (creating two write transactions obviously doesn't work)
            //NOTE: we don't support this scenario. A write transaction must commit or abort before a read transaction opens the same database.
            // auto transaction3 = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
            // auto db3 = transaction3.openDatabase("testTransactionVisibility", nullptr, false);
            // QCOMPARE(readValue(db3, "key1"), QByteArray("foo"));

            //Ensure we can still open further dbis in the write transaction
            auto db4 = transaction2.openDatabase("anotherDb", nullptr, false);
        }

    }
};

QTEST_MAIN(StorageTest)
#include "storagetest.moc"
