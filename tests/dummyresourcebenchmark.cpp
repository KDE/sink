#include <QtTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "clientapi.h"
#include "commands.h"
#include "entitybuffer.h"

static void removeFromDisk(const QString &name)
{
    Akonadi2::Storage store(Akonadi2::Store::storageLocation(), name, Akonadi2::Storage::ReadWrite);
    store.removeFromDisk();
}

class DummyResourceBenchmark : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        auto factory = Akonadi2::ResourceFactory::load("org.kde.dummy");
        QVERIFY(factory);
        removeFromDisk("org.kde.dummy");
        removeFromDisk("org.kde.dummy.userqueue");
        removeFromDisk("org.kde.dummy.synchronizerqueue");
        removeFromDisk("org.kde.dummy.index.uid");
    }

    void cleanup()
    {
        removeFromDisk("org.kde.dummy");
        removeFromDisk("org.kde.dummy.userqueue");
        removeFromDisk("org.kde.dummy.synchronizerqueue");
        removeFromDisk("org.kde.dummy.index.uid");
    }

    void testWriteToFacadeAndQueryByUid()
    {
        QTime time;
        time.start();
        int num = 10000;
        for (int i = 0; i < num; i++) {
            Akonadi2::Domain::Event event;
            event.setProperty("uid", "testuid");
            QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
            event.setProperty("summary", "summaryValue");
            Akonadi2::Store::create<Akonadi2::Domain::Event>(event, "org.kde.dummy");
        }
        auto appendTime = time.elapsed();

        //Ensure everything is processed
        {
            Akonadi2::Query query;
            query.resources << "org.kde.dummy";
            query.syncOnDemand = false;
            query.processAll = true;

            query.propertyFilter.insert("uid", "nonexistantuid");
            async::SyncListResult<Akonadi2::Domain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::Domain::Event>(query));
            result.exec();
        }
        auto allProcessedTime = time.elapsed();

        //Measure query
        {
            time.start();
            Akonadi2::Query query;
            query.resources << "org.kde.dummy";
            query.syncOnDemand = false;
            query.processAll = false;

            query.propertyFilter.insert("uid", "testuid");
            async::SyncListResult<Akonadi2::Domain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::Domain::Event>(query));
            result.exec();
            QCOMPARE(result.size(), num);
        }
        qDebug() << "Append to messagequeue " << appendTime;
        qDebug() << "All processed: " << allProcessedTime << "/sec " << num*1000/allProcessedTime;
        qDebug() << "Query Time: " << time.elapsed() << "/sec " << num*1000/time.elapsed();
    }
};

QTEST_MAIN(DummyResourceBenchmark)
#include "dummyresourcebenchmark.moc"
