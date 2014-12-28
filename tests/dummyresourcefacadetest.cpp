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

        //FIXME avoid sync somehow. No synchronizer access here (perhaps configure the instance above accordingly?)
        async::SyncListResult<Akonadi2::Domain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::Domain::Event>(query));
        result.exec();
        QCOMPARE(result.size(), 1);

        Akonadi2::Storage storage(testDataPath, dbName);
        storage.removeFromDisk();
    }

};

QTEST_MAIN(DummyResourceFacadeTest)
#include "dummyresourcefacadetest.moc"
