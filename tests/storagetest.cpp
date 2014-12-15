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
        for (int i = 0; i < count; i++) {
            //This should perhaps become an implementation detail of the db?
            if (i % 10000 == 0) {
                if (i > 0) {
                    storage.commitTransaction();
                }
                storage.startTransaction();
            }
            storage.write(keyPrefix + std::to_string(i), keyPrefix + std::to_string(i));
        }
        storage.commitTransaction();
    }

    bool verify(Akonadi2::Storage &storage, int i)
    {
        bool success = true;
        bool keyMatch = true;
        const auto reference = keyPrefix + std::to_string(i);
        storage.read(keyPrefix + std::to_string(i),
            [&keyMatch, &reference](const std::string &value) -> bool {
                if (value != reference) {
                    qDebug() << "Mismatch while reading";
                    keyMatch = false;
                }
                return keyMatch;
            },
            [&success](const Akonadi2::Storage::Error &) { success = false; }
            );
        return success && keyMatch;
    }

private Q_SLOTS:
    void initTestCase()
    {
        testDataPath = "./testdb";
        dbName = "test";
    }

    void cleanupTestCase()
    {
        Akonadi2::Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
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

        Akonadi2::Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
    }

    void testScan()
    {
        const int count = 100;
        populate(count);

        //ensure we can scan for values
        {
            int hit = 0;
            Akonadi2::Storage store(testDataPath, dbName);
            store.scan("", [&](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
                if (std::string(static_cast<char*>(keyValue), keySize) == "key50") {
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
            store.scan("key50", [&](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
                if (std::string(static_cast<char*>(keyValue), keySize) != "key50") {
                    foundInvalidValue = true;
                }
                hit++;
                return true;
            });
            QVERIFY(!foundInvalidValue);
            QCOMPARE(hit, 1);
        }

        Akonadi2::Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
    }

    void testConcurrentRead()
    {
        const int count = 10000;

        populate(count);

        bool error = false;
        //Try to concurrently read
        QList<QFuture<void> > futures;
        const int concurrencyLevel = 10;
        for (int num = 0; num < concurrencyLevel; num++) {
            futures << QtConcurrent::run([this, count, &error](){
                Akonadi2::Storage storage(testDataPath, dbName);
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

        Akonadi2::Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
    }
};

QTEST_MAIN(StorageTest)
#include "storagetest.moc"
