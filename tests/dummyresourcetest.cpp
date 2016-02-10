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

    QTime time;

private Q_SLOTS:
    void initTestCase()
    {
        Sink::Log::setDebugOutputLevel(Sink::Log::Trace);
        auto factory = Sink::ResourceFactory::load("org.kde.dummy");
        QVERIFY(factory);
        DummyResource::removeFromDisk("org.kde.dummy.instance1");
        ResourceConfig::addResource("org.kde.dummy.instance1", "org.kde.dummy");
    }

    void init()
    {
        qDebug();
        qDebug() << "-----------------------------------------";
        qDebug();
        time.start();
    }

    void cleanup()
    {
        qDebug() << "Test took " << time.elapsed();
        Sink::Store::removeDataFromDisk(QByteArray("org.kde.dummy.instance1")).exec().waitForFinished();
    }

    void testProperty()
    {
        Sink::ApplicationDomain::Event event;
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testWriteToFacadeAndQueryByUid()
    {
        Sink::ApplicationDomain::Event event("org.kde.dummy.instance1");
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");
        Sink::Store::create<Sink::ApplicationDomain::Event>(event).exec().waitForFinished();

        Sink::Query query;
        query.resources << "org.kde.dummy.instance1";

        //Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        query.propertyFilter.insert("uid", "testuid");
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
        QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Event::Ptr>();
        QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testWriteToFacadeAndQueryByUid2()
    {
        Sink::ApplicationDomain::Event event("org.kde.dummy.instance1");
        event.setProperty("summary", "summaryValue");

        event.setProperty("uid", "testuid");
        Sink::Store::create<Sink::ApplicationDomain::Event>(event).exec().waitForFinished();

        event.setProperty("uid", "testuid2");
        Sink::Store::create<Sink::ApplicationDomain::Event>(event).exec().waitForFinished();

        Sink::Query query;
        query.resources << "org.kde.dummy.instance1";

        //Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        query.propertyFilter.insert("uid", "testuid");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
        QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Event::Ptr>();

        qDebug() << value->getProperty("uid").toByteArray();
        QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testWriteToFacadeAndQueryBySummary()
    {
        Sink::ApplicationDomain::Event event("org.kde.dummy.instance1");

        event.setProperty("uid", "testuid");
        event.setProperty("summary", "summaryValue1");
        Sink::Store::create<Sink::ApplicationDomain::Event>(event).exec().waitForFinished();

        event.setProperty("uid", "testuid2");
        event.setProperty("summary", "summaryValue2");
        Sink::Store::create<Sink::ApplicationDomain::Event>(event).exec().waitForFinished();

        Sink::Query query;
        query.resources << "org.kde.dummy.instance1";

        //Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        query.propertyFilter.insert("summary", "summaryValue2");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
        QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Event::Ptr>();

        qDebug() << value->getProperty("uid").toByteArray();
        QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid2"));
    }

    void testResourceSync()
    {
        auto pipeline = QSharedPointer<Sink::Pipeline>::create("org.kde.dummy.instance1");
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
        Sink::Query query;
        query.resources << "org.kde.dummy.instance1";

        //Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
        QTRY_VERIFY(model->rowCount(QModelIndex()) >= 1);
        auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Event::Ptr>();

        QVERIFY(!value->getProperty("summary").toString().isEmpty());
        qDebug() << value->getProperty("summary").toString();
    }

    void testSyncAndFacadeMail()
    {
        Sink::Query query;
        query.resources << "org.kde.dummy.instance1";

        //Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(model->rowCount(QModelIndex()) >= 1);
        auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Mail::Ptr>();

        QVERIFY(!value->getProperty("subject").toString().isEmpty());
        qDebug() << value->getProperty("subject").toString();
    }

    void testWriteModifyDelete()
    {
        Sink::ApplicationDomain::Event event("org.kde.dummy.instance1");
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");
        Sink::Store::create<Sink::ApplicationDomain::Event>(event).exec().waitForFinished();

        Sink::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.propertyFilter.insert("uid", "testuid");

        //Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        //Test create
        Sink::ApplicationDomain::Event event2;
        {
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Event::Ptr>();

            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue"));
            event2 = *value;
        }

        event2.setProperty("uid", "testuid");
        event2.setProperty("summary", "summaryValue2");
        Sink::Store::modify<Sink::ApplicationDomain::Event>(event2).exec().waitForFinished();

        //Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        //Test modify
        {
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Event::Ptr>();

            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue2"));
        }

        Sink::Store::remove<Sink::ApplicationDomain::Event>(event2).exec().waitForFinished();

        //Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        //Test remove
        {
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QTRY_COMPARE(model->rowCount(QModelIndex()), 0);
        }
    }

    void testWriteModifyDeleteLive()
    {

        Sink::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.liveQuery = true;
        query.propertyFilter.insert("uid", "testuid");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());

        Sink::ApplicationDomain::Event event("org.kde.dummy.instance1");
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");
        Sink::Store::create<Sink::ApplicationDomain::Event>(event).exec().waitForFinished();

        //Test create
        Sink::ApplicationDomain::Event event2;
        {
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Event::Ptr>();
            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue"));
            event2 = *value;
        }

        event2.setProperty("uid", "testuid");
        event2.setProperty("summary", "summaryValue2");
        Sink::Store::modify<Sink::ApplicationDomain::Event>(event2).exec().waitForFinished();

        //Test modify
        {
            //TODO wait for a change signal
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Event::Ptr>();
            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue2"));
        }

        Sink::Store::remove<Sink::ApplicationDomain::Event>(event2).exec().waitForFinished();

        //Test remove
        {
            QTRY_COMPARE(model->rowCount(QModelIndex()), 0);
        }
    }

};

QTEST_MAIN(DummyResourceTest)
#include "dummyresourcetest.moc"
