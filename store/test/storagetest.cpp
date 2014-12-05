#include <QtTest>

#include <iostream>

#include <QDebug>
#include <QString>
#include <QtConcurrent/QtConcurrentRun>

#include "store/database.h"

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
        Database db(testDataPath, dbName);
        for (int i = 0; i < count; i++) {
            //This should perhaps become an implementation detail of the db?
            if (i % 10000 == 0) {
                if (i > 0) {
                    db.commitTransaction();
                }
                db.startTransaction();
            }
            db.write(keyPrefix + std::to_string(i), keyPrefix + std::to_string(i));
        }
        db.commitTransaction();
    }

    bool verify(Database &db, int i)
    {
        bool error = false;
        const auto reference = keyPrefix + std::to_string(i);
        if(!db.read(keyPrefix + std::to_string(i), [&error, &reference](const std::string &value) {
            if (value != reference) {
                qDebug() << "Mismatch while reading";
                error = true;
            }
        })) {
            return false;
        }
        return !error;
    }

private Q_SLOTS:
    void initTestCase()
    {
        testDataPath = "./testdb";
        dbName = "test";
    }

    void cleanupTestCase()
    {
        Database db(testDataPath, dbName);
        db.removeFromDisk();
    }


    void testRead()
    {
        const int count = 100;

        populate(count);

        //ensure we can read everything back correctly
        {
            Database db(testDataPath, dbName);
            for (int i = 0; i < count; i++) {
                QVERIFY(verify(db, i));
            }
        }

        Database db(testDataPath, dbName);
        db.removeFromDisk();
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
                Database db(testDataPath, dbName);
                for (int i = 0; i < count; i++) {
                    if (!verify(db, i)) {
                        qWarning() << "invalid value";
                        break;
                    }
                }
            });
        }
        for(auto future : futures) {
            future.waitForFinished();
        }

        Database db(testDataPath, dbName);
        db.removeFromDisk();
    }
};

QTEST_MAIN(StorageTest)
#include "storagetest.moc"
