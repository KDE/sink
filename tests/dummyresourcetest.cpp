#include <QtTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "clientapi.h"
#include "commands.h"
#include "entitybuffer.h"
#include "resourceconfig.h"
#include "modelresult.h"
#include "pipeline.h"
#include "log.h"

/**
 * Test of complete system using the dummy resource.
 * 
 * This test requires the dummy resource installed.
 */
class DummyResourceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        Akonadi2::Log::setDebugOutputLevel(Akonadi2::Log::Trace);
        auto factory = Akonadi2::ResourceFactory::load("org.kde.dummy");
        QVERIFY(factory);
        DummyResource::removeFromDisk("org.kde.dummy.instance1");
        ResourceConfig::addResource("org.kde.dummy.instance1", "org.kde.dummy");
    }

    void cleanup()
    {
        Akonadi2::Store::shutdown(QByteArray("org.kde.dummy.instance1")).exec().waitForFinished();
        DummyResource::removeFromDisk("org.kde.dummy.instance1");
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
        Akonadi2::ApplicationDomain::Event event("org.kde.dummy.instance1");
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event).exec().waitForFinished();

        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        query.propertyFilter.insert("uid", "testuid");
        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
        QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        auto value = model->index(0, 0, QModelIndex()).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Event::Ptr>();
        QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testWriteToFacadeAndQueryByUid2()
    {
        Akonadi2::ApplicationDomain::Event event("org.kde.dummy.instance1");
        event.setProperty("summary", "summaryValue");

        event.setProperty("uid", "testuid");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event).exec().waitForFinished();

        event.setProperty("uid", "testuid2");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event).exec().waitForFinished();

        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        query.propertyFilter.insert("uid", "testuid");

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
        QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        auto value = model->index(0, 0, QModelIndex()).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Event::Ptr>();

        qDebug() << value->getProperty("uid").toByteArray();
        QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testWriteToFacadeAndQueryBySummary()
    {
        Akonadi2::ApplicationDomain::Event event("org.kde.dummy.instance1");

        event.setProperty("uid", "testuid");
        event.setProperty("summary", "summaryValue1");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event).exec().waitForFinished();

        event.setProperty("uid", "testuid2");
        event.setProperty("summary", "summaryValue2");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event).exec().waitForFinished();

        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        query.propertyFilter.insert("summary", "summaryValue2");

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
        QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        auto value = model->index(0, 0, QModelIndex()).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Event::Ptr>();

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

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
        QTRY_VERIFY(model->rowCount(QModelIndex()) >= 1);
        auto value = model->index(0, 0, QModelIndex()).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Event::Ptr>();

        QVERIFY(!value->getProperty("summary").toString().isEmpty());
        qDebug() << value->getProperty("summary").toString();
    }

    void testSyncAndFacadeMail()
    {
        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = true;
        query.processAll = true;

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(model->rowCount(QModelIndex()) >= 1);
        auto value = model->index(0, 0, QModelIndex()).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Mail::Ptr>();

        QVERIFY(!value->getProperty("subject").toString().isEmpty());
        qDebug() << value->getProperty("subject").toString();
    }

    void testWriteModifyDelete()
    {
        Akonadi2::ApplicationDomain::Event event("org.kde.dummy.instance1");
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event).exec().waitForFinished();

        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;
        query.propertyFilter.insert("uid", "testuid");

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        //Test create
        Akonadi2::ApplicationDomain::Event event2;
        {
            auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto value = model->index(0, 0, QModelIndex()).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Event::Ptr>();

            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue"));
            event2 = *value;
        }

        event2.setProperty("uid", "testuid");
        event2.setProperty("summary", "summaryValue2");
        Akonadi2::Store::modify<Akonadi2::ApplicationDomain::Event>(event2).exec().waitForFinished();

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        //Test modify
        {
            auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto value = model->index(0, 0, QModelIndex()).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Event::Ptr>();

            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue2"));
        }

        Akonadi2::Store::remove<Akonadi2::ApplicationDomain::Event>(event2).exec().waitForFinished();

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        //Test remove
        {
            auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
            //TODO ensure the initial query is done
            QTRY_COMPARE(model->rowCount(QModelIndex()), 0);
        }
    }

    void testWriteModifyDeleteLive()
    {

        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;
        query.liveQuery = true;
        query.propertyFilter.insert("uid", "testuid");

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
        //TODO ensure the initial query is done

        Akonadi2::ApplicationDomain::Event event("org.kde.dummy.instance1");
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");
        Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event).exec().waitForFinished();

        //Test create
        Akonadi2::ApplicationDomain::Event event2;
        {
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto value = model->index(0, 0, QModelIndex()).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Event::Ptr>();
            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue"));
            event2 = *value;
        }

        event2.setProperty("uid", "testuid");
        event2.setProperty("summary", "summaryValue2");
        Akonadi2::Store::modify<Akonadi2::ApplicationDomain::Event>(event2).exec().waitForFinished();

        //Test modify
        {
            //TODO wait for a change signal
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto value = model->index(0, 0, QModelIndex()).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Event::Ptr>();
            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue2"));
        }

        Akonadi2::Store::remove<Akonadi2::ApplicationDomain::Event>(event2).exec().waitForFinished();

        //Test remove
        {
            QTRY_COMPARE(model->rowCount(QModelIndex()), 0);
        }
    }

};

QTEST_MAIN(DummyResourceTest)
#include "dummyresourcetest.moc"
