#include <QtTest>

#include <iostream>

#include <QDebug>
#include <QString>
#include <QtConcurrent/QtConcurrentRun>

#include "common/storage.h"

class StorageTest : public QObject
{
    Q_OBJECT
private:
    //This should point to a directory on disk and not a ramdisk (since we're measuring performance)
    QString testDataPath;
    QString dbName;
    const char *keyPrefix = "key";

    void populate(int count)
    {
        Akonadi2::Storage storage(testDataPath, dbName, Akonadi2::Storage::ReadWrite);
        auto transaction = storage.createTransaction(Akonadi2::Storage::ReadWrite);
        for (int i = 0; i < count; i++) {
            //This should perhaps become an implementation detail of the db?
            if (i % 10000 == 0) {
                if (i > 0) {
                    transaction.commit();
                    transaction = std::move(storage.createTransaction(Akonadi2::Storage::ReadWrite));
                }
            }
            transaction.write(keyPrefix + QByteArray::number(i), keyPrefix + QByteArray::number(i));
        }
        transaction.commit();
    }

    bool verify(Akonadi2::Storage &storage, int i)
    {
        bool success = true;
        bool keyMatch = true;
        const auto reference = keyPrefix + QByteArray::number(i);
        storage.createTransaction(Akonadi2::Storage::ReadOnly).scan(keyPrefix + QByteArray::number(i),
            [&keyMatch, &reference](const QByteArray &key, const QByteArray &value) -> bool {
                if (value != reference) {
                    qDebug() << "Mismatch while reading";
                    keyMatch = false;
                }
                return keyMatch;
            },
            [&success](const Akonadi2::Storage::Error &error) {
                qDebug() << error.message;
                success = false;
            }
        );
        return success && keyMatch;
    }

private Q_SLOTS:
    void initTestCase()
    {
        testDataPath = "./testdb";
        dbName = "test";
        Akonadi2::Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
    }

    void cleanup()
    {
        Akonadi2::Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
    }

    void testCleanup()
    {
        populate(1);
        Akonadi2::Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
        QFileInfo info(testDataPath + "/" + dbName);
        QVERIFY(!info.exists());
    }

    void testRead()
    {
        const int count = 100;

        populate(count);

        //ensure we can read everything back correctly
        {
            Akonadi2::Storage storage(testDataPath, dbName);
            for (int i = 0; i < count; i++) {
                QVERIFY(verify(storage, i));
            }
        }
    }

    void testScan()
    {
        const int count = 100;
        populate(count);

        //ensure we can scan for values
        {
            int hit = 0;
            Akonadi2::Storage store(testDataPath, dbName);
            store.createTransaction(Akonadi2::Storage::ReadOnly).scan("", [&](const QByteArray &key, const QByteArray &value) -> bool {
                if (key == "key50") {
                    hit++;
                }
                return true;
            });
            QCOMPARE(hit, 1);
        }

        //ensure we can read a single value
        {
            int hit = 0;
            bool foundInvalidValue = false;
            Akonadi2::Storage store(testDataPath, dbName);
            store.createTransaction(Akonadi2::Storage::ReadOnly).scan("key50", [&](const QByteArray &key, const QByteArray &value) -> bool {
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
        Akonadi2::Storage store(testDataPath, dbName, Akonadi2::Storage::ReadWrite);
        auto transaction = store.createTransaction(Akonadi2::Storage::ReadWrite);
        transaction.scan("key1", [&](const QByteArray &key, const QByteArray &value) -> bool {
            transaction.remove(key, [](const Akonadi2::Storage::Error &) {
                QVERIFY(false);
            });
            return false;
        });
    }

    void testNestedTransactions()
    {
        populate(3);
        Akonadi2::Storage store(testDataPath, dbName, Akonadi2::Storage::ReadWrite);
        store.createTransaction(Akonadi2::Storage::ReadOnly).scan("key1", [&](const QByteArray &key, const QByteArray &value) -> bool {
            store.createTransaction(Akonadi2::Storage::ReadWrite).remove(key, [](const Akonadi2::Storage::Error &) {
                QVERIFY(false);
            });
            return false;
        });
    }

    void testReadEmptyDb()
    {
        bool gotResult = false;
        bool gotError = false;
        Akonadi2::Storage store(testDataPath, dbName, Akonadi2::Storage::ReadWrite);
        auto transaction = store.createTransaction(Akonadi2::Storage::ReadOnly);
        auto db = transaction.openDatabase("default", [&](const Akonadi2::Storage::Error &error) {
            qDebug() << error.message;
            gotError = true;
        });
        int numValues = db.scan("", [&](const QByteArray &key, const QByteArray &value) -> bool {
            gotResult = true;
            return false;
        },
        [&](const Akonadi2::Storage::Error &error) {
            qDebug() << error.message;
            gotError = true;
        });
        QCOMPARE(numValues, 0);
        QVERIFY(!gotResult);
        QVERIFY(!gotError);
    }

    void testConcurrentRead()
    {
        //With a count of 10000 this test is more likely to expose problems, but also takes some time to execute.
        const int count = 1000;

        populate(count);
        // QTest::qWait(500);

        //We repeat the test a bunch of times since failing is relatively random
        for (int tries = 0; tries < 10; tries++) {
            bool error = false;
            //Try to concurrently read
            QList<QFuture<void> > futures;
            const int concurrencyLevel = 20;
            for (int num = 0; num < concurrencyLevel; num++) {
                futures << QtConcurrent::run([this, count, &error](){
                    Akonadi2::Storage storage(testDataPath, dbName, Akonadi2::Storage::ReadOnly);
                    Akonadi2::Storage storage2(testDataPath, dbName + "2", Akonadi2::Storage::ReadOnly);
                    for (int i = 0; i < count; i++) {
                        if (!verify(storage, i)) {
                            error = true;
                            break;
                        }
                    }
                });
            }
            for(auto future : futures) {
                future.waitForFinished();
            }
            QVERIFY(!error);
        }

        {
            Akonadi2::Storage storage(testDataPath, dbName);
            storage.removeFromDisk();
            Akonadi2::Storage storage2(testDataPath, dbName + "2");
            storage2.removeFromDisk();
        }
    }

    void testNoDuplicates()
    {
        bool gotResult = false;
        bool gotError = false;
        Akonadi2::Storage store(testDataPath, dbName, Akonadi2::Storage::ReadWrite);
        auto transaction = store.createTransaction(Akonadi2::Storage::ReadWrite);
        auto db = transaction.openDatabase("default", nullptr, false);
        db.write("key","value");
        db.write("key","value");

        int numValues = db.scan("", [&](const QByteArray &key, const QByteArray &value) -> bool {
            gotResult = true;
            return true;
        },
        [&](const Akonadi2::Storage::Error &error) {
            qDebug() << error.message;
            gotError = true;
        });

        QCOMPARE(numValues, 1);
        QVERIFY(!gotError);
    }

    void testDuplicates()
    {
        bool gotResult = false;
        bool gotError = false;
        Akonadi2::Storage store(testDataPath, dbName, Akonadi2::Storage::ReadWrite);
        auto transaction = store.createTransaction(Akonadi2::Storage::ReadWrite);
        auto db = transaction.openDatabase("default", nullptr, true);
        db.write("key","value1");
        db.write("key","value2");
        int numValues = db.scan("key", [&](const QByteArray &key, const QByteArray &value) -> bool {
            gotResult = true;
            return true;
        },
        [&](const Akonadi2::Storage::Error &error) {
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
        Akonadi2::Storage store(testDataPath, dbName, Akonadi2::Storage::ReadOnly);
        int numValues = store.createTransaction(Akonadi2::Storage::ReadOnly).openDatabase("test").scan("", [&](const QByteArray &key, const QByteArray &value) -> bool {
            gotResult = true;
            return false;
        },
        [&](const Akonadi2::Storage::Error &error) {
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
        Akonadi2::Storage store(testDataPath, dbName, Akonadi2::Storage::ReadWrite);
        store.createTransaction(Akonadi2::Storage::ReadWrite).openDatabase("test").write("key1", "value1", [&](const Akonadi2::Storage::Error &error) {
            qDebug() << error.message;
            gotError = true;
        });
        QVERIFY(!gotError);
    }

    void testWriteDuplicatesToNamedDb()
    {
        bool gotError = false;
        Akonadi2::Storage store(testDataPath, dbName, Akonadi2::Storage::ReadWrite);
        store.createTransaction(Akonadi2::Storage::ReadWrite).openDatabase("test", nullptr, true).write("key1", "value1", [&](const Akonadi2::Storage::Error &error) {
            qDebug() << error.message;
            gotError = true;
        });
        QVERIFY(!gotError);
    }

    //By default we want only exact matches
    void testSubstringKeys()
    {
        Akonadi2::Storage store(testDataPath, dbName, Akonadi2::Storage::ReadWrite);
        auto transaction = store.createTransaction(Akonadi2::Storage::ReadWrite);
        auto db = transaction.openDatabase("test", nullptr, true);
        db.write("sub","value1");
        db.write("subsub","value2");
        int numValues = db.scan("sub", [&](const QByteArray &key, const QByteArray &value) -> bool {
            return true;
        });

        QCOMPARE(numValues, 1);
    }

    //Ensure we don't retrieve a key that is greater than the current key. We only want equal keys.
    void testKeyRange()
    {
        Akonadi2::Storage store(testDataPath, dbName, Akonadi2::Storage::ReadWrite);
        auto transaction = store.createTransaction(Akonadi2::Storage::ReadWrite);
        auto db = transaction.openDatabase("test", nullptr, true);
        db.write("sub1","value1");
        int numValues = db.scan("sub", [&](const QByteArray &key, const QByteArray &value) -> bool {
            return true;
        });

        QCOMPARE(numValues, 0);
    }
};

QTEST_MAIN(StorageTest)
#include "storagetest.moc"
