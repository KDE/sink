#include <QtTest>

#include <QString>

#include "testimplementations.h"

#include <common/facade.h>
#include <common/domainadaptor.h>
#include <common/resultprovider.h>
#include <common/synclistresult.h>

//Replace with something different
#include "event_generated.h"


/**
 * Test for the generic facade implementation.
 *
 * This test doesn't use the actual storage and thus only tests the update logic of the facade.
 * //FIXME this now uses the actual storage
 */
class GenericFacadeTest : public QObject
{
    Q_OBJECT
private slots:

    void init()
    {
        Sink::Log::setDebugOutputLevel(Sink::Log::Trace);
    }

    void testLoad()
    {
        Sink::Query query;
        query.liveQuery = false;

        auto resultSet = QSharedPointer<Sink::ResultProvider<Sink::ApplicationDomain::Event::Ptr> >::create();
        auto resourceAccess = QSharedPointer<TestResourceAccess>::create();
        // storage->mResults << Sink::ApplicationDomain::Event::Ptr::create();
        TestResourceFacade facade("identifier", resourceAccess);

        async::SyncListResult<Sink::ApplicationDomain::Event::Ptr> result(resultSet->emitter());

        facade.load(query, *resultSet).exec().waitForFinished();
        resultSet->initialResultSetComplete();

        //We have to wait for the events that deliver the results to be processed by the eventloop
        result.exec();

        QCOMPARE(result.size(), 1);
    }

    void testLiveQuery()
    {
        Sink::Query query;
        query.liveQuery = true;

        auto resultSet = QSharedPointer<Sink::ResultProvider<Sink::ApplicationDomain::Event::Ptr> >::create();
        auto resourceAccess = QSharedPointer<TestResourceAccess>::create();
        // storage->mResults << Sink::ApplicationDomain::Event::Ptr::create();
        TestResourceFacade facade("identifier", resourceAccess);

        async::SyncListResult<Sink::ApplicationDomain::Event::Ptr> result(resultSet->emitter());

        facade.load(query, *resultSet).exec().waitForFinished();
        resultSet->initialResultSetComplete();

        result.exec();
        QCOMPARE(result.size(), 1);

        //Enter a second result
        // storage->mResults.clear();
        // storage->mResults << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource", "id2", 0, QSharedPointer<Sink::ApplicationDomain::BufferAdaptor>());
        // storage->mLatestRevision = 2;
        resourceAccess->emit revisionChanged(2);

        //Hack to get event loop in synclistresult to abort again
        resultSet->initialResultSetComplete();
        result.exec();

        QCOMPARE(result.size(), 2);
    }

    void testLiveQueryModify()
    {
        Sink::Query query;
        query.liveQuery = true;

        auto resultSet = QSharedPointer<Sink::ResultProvider<Sink::ApplicationDomain::Event::Ptr> >::create();
        auto resourceAccess = QSharedPointer<TestResourceAccess>::create();
        auto entity = QSharedPointer<Sink::ApplicationDomain::Event>::create("resource", "id2", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        entity->setProperty("test", "test1");
        // storage->mResults << entity;
        TestResourceFacade facade("identifier", resourceAccess);

        async::SyncListResult<Sink::ApplicationDomain::Event::Ptr> result(resultSet->emitter());

        facade.load(query, *resultSet).exec().waitForFinished();
        resultSet->initialResultSetComplete();

        result.exec();
        QCOMPARE(result.size(), 1);

        //Modify the entity again
        // storage->mResults.clear();
        entity = QSharedPointer<Sink::ApplicationDomain::Event>::create("resource", "id2", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        entity->setProperty("test", "test2");
        // storage->mModifications << entity;
        // storage->mLatestRevision = 2;
        resourceAccess->emit revisionChanged(2);

        //Hack to get event loop in synclistresult to abort again
        resultSet->initialResultSetComplete();
        result.exec();

        QCOMPARE(result.size(), 1);
        QCOMPARE(result.first()->getProperty("test").toByteArray(), QByteArray("test2"));
    }

    void testLiveQueryRemove()
    {
        Sink::Query query;
        query.liveQuery = true;

        auto resultSet = QSharedPointer<Sink::ResultProvider<Sink::ApplicationDomain::Event::Ptr> >::create();
        auto resourceAccess = QSharedPointer<TestResourceAccess>::create();
        auto entity = QSharedPointer<Sink::ApplicationDomain::Event>::create("resource", "id2", 0, QSharedPointer<Sink::ApplicationDomain::BufferAdaptor>());
        // storage->mResults << entity;
        TestResourceFacade facade("identifier", resourceAccess);

        async::SyncListResult<Sink::ApplicationDomain::Event::Ptr> result(resultSet->emitter());

        facade.load(query, *resultSet).exec().waitForFinished();
        resultSet->initialResultSetComplete();

        result.exec();
        QCOMPARE(result.size(), 1);

        //Remove the entity again
        // storage->mResults.clear();
        // storage->mRemovals << entity;
        // storage->mLatestRevision = 2;
        resourceAccess->emit revisionChanged(2);

        //Hack to get event loop in synclistresult to abort again
        resultSet->initialResultSetComplete();
        result.exec();

        QCOMPARE(result.size(), 0);
    }
};

QTEST_MAIN(GenericFacadeTest)
#include "genericfacadetest.moc"
