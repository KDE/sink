#include <QtTest>

#include <iostream>

#include <QDebug>
#include <QString>
#include <QtConcurrent/QtConcurrentRun>

#include "common/storage.h"
#include "dummyresource/facade.h"

class DummyResourceFacadeTest : public QObject
{
    Q_OBJECT
private:
    QString testDataPath;
    QString dbName;
    const char *keyPrefix = "key";

    void populate(int count)
    {
        Akonadi2::Storage storage(testDataPath, dbName, Akonadi2::Storage::ReadWrite);
        for (int i = 0; i < count; i++) {
            storage.write(keyPrefix + std::to_string(i), keyPrefix + std::to_string(i));
        }
        storage.commitTransaction();
    }

private Q_SLOTS:
    void initTestCase()
    {
        testDataPath = Akonadi2::Store::storageLocation();
        dbName = "dummyresource";
        Akonadi2::FacadeFactory::instance().registerFacade<Akonadi2::Domain::Event, DummyResourceFacade>("dummyresource", []() {
            return new DummyResourceFacade();
        });
    }

    void cleanupTestCase()
    {
        Akonadi2::Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
    }

    void testScan()
    {
        const int count = 100;
        populate(count);

        Akonadi2::Query query;
        query.ids << "key50";
        query.resources << "dummyresource";

        auto result = Akonadi2::Store::load<Akonadi2::Domain::Event>(query);
        bool complete = false;
        QVector<Akonadi2::Domain::Event::Ptr> results;
        result->onAdded([&results](const Akonadi2::Domain::Event::Ptr &e) {
            results << e;
        });
        result->onComplete([&complete]() {
            complete = true;
        });
        QTRY_VERIFY(complete);
        QCOMPARE(results.size(), 1);

        Akonadi2::Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
    }

};

QTEST_MAIN(DummyResourceFacadeTest)
#include "dummyresourcefacadetest.moc"
