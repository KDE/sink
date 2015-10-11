#include <QtTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "clientapi.h"
#include "synclistresult.h"
#include "commands.h"
#include "entitybuffer.h"
#include "resourceconfig.h"
#include "pipeline.h"
#include "log.h"

static void removeFromDisk(const QString &name)
{
    Akonadi2::Storage store(Akonadi2::Store::storageLocation(), name, Akonadi2::Storage::ReadWrite);
    store.removeFromDisk();
}

class DummyResourceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        Akonadi2::Log::setDebugOutputLevel(Akonadi2::Log::Trace);
        auto factory = Akonadi2::ResourceFactory::load("org.kde.dummy");
        QVERIFY(factory);
        removeFromDisk("org.kde.dummy.instance1");
        removeFromDisk("org.kde.dummy.instance1.userqueue");
        removeFromDisk("org.kde.dummy.instance1.synchronizerqueue");
        removeFromDisk("org.kde.dummy.instance1.event.index.uid");
        ResourceConfig::addResource("org.kde.dummy.instance1", "org.kde.dummy");
    }

    void cleanup()
    {
        Akonadi2::Store::shutdown("org.kde.dummy.instance1");
        removeFromDisk("org.kde.dummy.instance1");
        removeFromDisk("org.kde.dummy.instance1.userqueue");
        removeFromDisk("org.kde.dummy.instance1.synchronizerqueue");
        removeFromDisk("org.kde.dummy.instance1.event.index.uid");
        auto factory = Akonadi2::ResourceFactory::load("org.kde.dummy");
        QVERIFY(factory);
    }

    void init()
    {
        qDebug();
        qDebug() << "-----------------------------------------";
        qDebug();
    }

    void testProperty()
    {
        Akonadi2::ApplicationDomain::Event event;
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testWriteToFacadeAndQueryByUid()
    {
        Akonadi2::ApplicationDomain::Event event;
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event, "org.kde.dummy.instance1").exec().waitForFinished();

        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;

        query.propertyFilter.insert("uid", "testuid");
        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
        result.exec();
        QCOMPARE(result.size(), 1);
        auto value = result.first();
        QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testWriteToFacadeAndQueryByUid2()
    {
        Akonadi2::ApplicationDomain::Event event;
        event.setProperty("summary", "summaryValue");

        event.setProperty("uid", "testuid");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event, "org.kde.dummy.instance1").exec().waitForFinished();

        event.setProperty("uid", "testuid2");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event, "org.kde.dummy.instance1").exec().waitForFinished();

        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;

        query.propertyFilter.insert("uid", "testuid");
        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
        result.exec();
        QCOMPARE(result.size(), 1);
        auto value = result.first();
        qDebug() << value->getProperty("uid").toByteArray();
        QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testWriteToFacadeAndQueryBySummary()
    {
        Akonadi2::ApplicationDomain::Event event;

        event.setProperty("uid", "testuid");
        event.setProperty("summary", "summaryValue1");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event, "org.kde.dummy.instance1").exec().waitForFinished();

        event.setProperty("uid", "testuid2");
        event.setProperty("summary", "summaryValue2");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event, "org.kde.dummy.instance1").exec().waitForFinished();

        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;

        query.propertyFilter.insert("summary", "summaryValue2");
        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
        result.exec();
        QCOMPARE(result.size(), 1);
        auto value = result.first();
        qDebug() << value->getProperty("uid").toByteArray();
        QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid2"));
    }

    void testResourceSync()
    {
        auto pipeline = QSharedPointer<Akonadi2::Pipeline>::create("org.kde.dummy.instance1");
        DummyResource resource("org.kde.dummy.instance1", pipeline);
        auto job = resource.synchronizeWithSource();
        //TODO pass in optional timeout?
        auto future = job.exec();
        future.waitForFinished();
        QVERIFY(!future.errorCode());
        QVERIFY(future.isFinished());
        QVERIFY(!resource.error());
        auto processAllMessagesFuture = resource.processAllMessages().exec();
        processAllMessagesFuture.waitForFinished();
    }

    void testSyncAndFacade()
    {
        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = true;
        query.processAll = true;

        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
        result.exec();
        QVERIFY(!result.isEmpty());
        auto value = result.first();
        QVERIFY(!value->getProperty("summary").toString().isEmpty());
        qDebug() << value->getProperty("summary").toString();
    }

    void testSyncAndFacadeMail()
    {
        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = true;
        query.processAll = true;

        async::SyncListResult<Akonadi2::ApplicationDomain::Mail::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Mail>(query));
        result.exec();
        QVERIFY(!result.isEmpty());
        auto value = result.first();
        QVERIFY(!value->getProperty("subject").toString().isEmpty());
        qDebug() << value->getProperty("subject").toString();
    }

    void testWriteModifyDelete()
    {
        Akonadi2::ApplicationDomain::Event event;
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event, "org.kde.dummy.instance1").exec().waitForFinished();

        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;
        query.propertyFilter.insert("uid", "testuid");

        //Test create
        Akonadi2::ApplicationDomain::Event event2;
        {
            async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
            result.exec();
            QCOMPARE(result.size(), 1);
            auto value = result.first();
            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue"));
            event2 = *value;
        }

        event2.setProperty("uid", "testuid");
        event2.setProperty("summary", "summaryValue2");
        Akonadi2::Store::modify<Akonadi2::ApplicationDomain::Event>(event2, "org.kde.dummy.instance1").exec().waitForFinished();

        //Test modify
        {
            async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
            result.exec();
            QCOMPARE(result.size(), 1);
            auto value = result.first();
            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue2"));
        }

        Akonadi2::Store::remove<Akonadi2::ApplicationDomain::Event>(event2, "org.kde.dummy.instance1").exec().waitForFinished();

        //Test remove
        {
            async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
            result.exec();
            QCOMPARE(result.size(), 0);
        }
    }

};

QTEST_MAIN(DummyResourceTest)
#include "dummyresourcetest.moc"
