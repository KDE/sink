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
        Storage storage(testDataPath, dbName, Storage::ReadWrite);
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

    bool verify(Storage &storage, int i)
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
            [&success](const Storage::Error &) { success = false; }
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
        Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
    }

    void testRead()
    {
        const int count = 100;

        populate(count);

        //ensure we can read everything back correctly
        {
            Storage storage(testDataPath, dbName);
            for (int i = 0; i < count; i++) {
                QVERIFY(verify(storage, i));
            }
        }

        Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
    }

    void testConcurrentRead()
    {
        const int count = 10000;

        populate(count);

        //Try to concurrently read
        QList<QFuture<void> > futures;
        const int concurrencyLevel = 4;
        for (int num = 0; num < concurrencyLevel; num++) {
            futures << QtConcurrent::run([this, count](){
                Storage storage(testDataPath, dbName);
                for (int i = 0; i < count; i++) {
                    if (!verify(storage, i)) {
                        qWarning() << "invalid value";
                        break;
                    }
                }
            });
        }
        for(auto future : futures) {
            future.waitForFinished();
        }

        Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
    }
};

QTEST_MAIN(StorageTest)
#include "storagetest.moc"
