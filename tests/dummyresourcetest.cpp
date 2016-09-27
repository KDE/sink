#include <QtTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "store.h"
#include "commands.h"
#include "entitybuffer.h"
#include "resourceconfig.h"
#include "resourcecontrol.h"
#include "modelresult.h"
#include "pipeline.h"
#include "log.h"
#include "test.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

/**
 * Test of complete system using the dummy resource.
 *
 * This test requires the dummy resource installed.
 */
class DummyResourceTest : public QObject
{
    Q_OBJECT

    QTime time;

private slots:
    void initTestCase()
    {
        Sink::Test::initTest();
        auto factory = Sink::ResourceFactory::load("sink.dummy");
        QVERIFY(factory);
        ::DummyResource::removeFromDisk("sink.dummy.instance1");
        ResourceConfig::addResource("sink.dummy.instance1", "sink.dummy");
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
        Sink::Store::removeDataFromDisk(QByteArray("sink.dummy.instance1")).exec().waitForFinished();
    }

    void testProperty()
    {
        Event event;
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testWriteToFacadeAndQueryByUid()
    {
        Event event("sink.dummy.instance1");
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");
        Sink::Store::create<Event>(event).exec().waitForFinished();

        auto query = Query::ResourceFilter("sink.dummy.instance1") ;

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Event>(query.filter<Event::Uid>("testuid"));
        QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Event::Ptr>();
        QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testWriteToFacadeAndQueryByUid2()
    {
        Event event("sink.dummy.instance1");
        event.setProperty("summary", "summaryValue");

        event.setProperty("uid", "testuid");
        Sink::Store::create<Event>(event).exec().waitForFinished();

        event.setProperty("uid", "testuid2");
        Sink::Store::create<Event>(event).exec().waitForFinished();

        auto query = Query::ResourceFilter("sink.dummy.instance1") ;

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Event>(query.filter<Event::Uid>("testuid"));
        QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Event::Ptr>();

        qDebug() << value->getProperty("uid").toByteArray();
        QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testWriteToFacadeAndQueryBySummary()
    {
        Event event("sink.dummy.instance1");

        event.setProperty("uid", "testuid");
        event.setProperty("summary", "summaryValue1");
        Sink::Store::create<Event>(event).exec().waitForFinished();

        event.setProperty("uid", "testuid2");
        event.setProperty("summary", "summaryValue2");
        Sink::Store::create<Event>(event).exec().waitForFinished();

        auto query = Query::ResourceFilter("sink.dummy.instance1") ;

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Event>(query.filter<Event::Summary>("summaryValue2"));
        QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Event::Ptr>();

        qDebug() << value->getProperty("uid").toByteArray();
        QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid2"));
    }

    void testResourceSync()
    {
        auto pipeline = QSharedPointer<Sink::Pipeline>::create("sink.dummy.instance1");
        ::DummyResource resource("sink.dummy.instance1", pipeline);
        auto job = resource.synchronizeWithSource();
        // TODO pass in optional timeout?
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
        const auto query = Query::ResourceFilter("sink.dummy.instance1");

        // Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Event>(query);
        QTRY_VERIFY(model->rowCount(QModelIndex()) >= 1);
        auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Event::Ptr>();

        QVERIFY(!value->getProperty("summary").toString().isEmpty());
        qDebug() << value->getProperty("summary").toString();
    }

    void testSyncAndFacadeMail()
    {
        auto query = Query::ResourceFilter("sink.dummy.instance1");
        query.request<Mail::Subject>();

        // Ensure all local data is processed
        Sink::Store::synchronize(query).exec().waitForFinished();
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->rowCount(QModelIndex()) >= 1);
        auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Mail::Ptr>();

        qWarning() << value->getSubject() << value->identifier();
        QVERIFY(!value->getSubject().isEmpty());
    }

    void testWriteModifyDelete()
    {
        Event event("sink.dummy.instance1");
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");
        Sink::Store::create<Event>(event).exec().waitForFinished();

        auto query = Query::ResourceFilter("sink.dummy.instance1").filter<Event::Uid>("testuid");

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        // Test create
        Event event2;
        {
            auto model = Sink::Store::loadModel<Event>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Event::Ptr>();

            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue"));
            event2 = *value;
        }

        event2.setProperty("uid", "testuid");
        event2.setProperty("summary", "summaryValue2");
        Sink::Store::modify<Event>(event2).exec().waitForFinished();

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        // Test modify
        {
            auto model = Sink::Store::loadModel<Event>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Event::Ptr>();

            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue2"));
        }

        Sink::Store::remove<Event>(event2).exec().waitForFinished();

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        // Test remove
        {
            auto model = Sink::Store::loadModel<Event>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QTRY_COMPARE(model->rowCount(QModelIndex()), 0);
        }
    }

    void testWriteModifyDeleteLive()
    {
        auto query = Query::ResourceFilter("sink.dummy.instance1");
        query.liveQuery = true;
        query.filter<Event::Uid>("testuid");

        auto model = Sink::Store::loadModel<Event>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());

        Event event("sink.dummy.instance1");
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");
        Sink::Store::create<Event>(event).exec().waitForFinished();

        // Test create
        Event event2;
        {
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Event::Ptr>();
            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue"));
            event2 = *value;
        }

        event2.setProperty("uid", "testuid");
        event2.setProperty("summary", "summaryValue2");
        Sink::Store::modify<Event>(event2).exec().waitForFinished();

        // Test modify
        {
            // TODO wait for a change signal
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto value = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Event::Ptr>();
            QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
            QCOMPARE(value->getProperty("summary").toByteArray(), QByteArray("summaryValue2"));
        }

        Sink::Store::remove<Event>(event2).exec().waitForFinished();

        // Test remove
        {
            QTRY_COMPARE(model->rowCount(QModelIndex()), 0);
        }
    }
};

QTEST_MAIN(DummyResourceTest)
#include "dummyresourcetest.moc"
