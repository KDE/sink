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
 */
class GenericFacadeTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:

    void init()
    {
        Akonadi2::Log::setDebugOutputLevel(Akonadi2::Log::Trace);
    }

    void testLoad()
    {
        Akonadi2::Query query;
        query.liveQuery = false;

        auto resultSet = QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> >::create();
        auto storage = QSharedPointer<TestEntityStorage>::create("identifier");
        auto resourceAccess = QSharedPointer<TestResourceAccess>::create();
        storage->mResults << Akonadi2::ApplicationDomain::Event::Ptr::create();
        TestResourceFacade facade("identifier", storage, resourceAccess);

        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(resultSet->emitter());

        facade.load(query, resultSet).exec().waitForFinished();
        resultSet->initialResultSetComplete();

        //We have to wait for the events that deliver the results to be processed by the eventloop
        result.exec();

        QCOMPARE(result.size(), 1);
    }

    void testLiveQuery()
    {
        Akonadi2::Query query;
        query.liveQuery = true;

        auto resultSet = QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> >::create();
        auto storage = QSharedPointer<TestEntityStorage>::create("identifier");
        auto resourceAccess = QSharedPointer<TestResourceAccess>::create();
        storage->mResults << Akonadi2::ApplicationDomain::Event::Ptr::create();
        TestResourceFacade facade("identifier", storage, resourceAccess);

        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(resultSet->emitter());

        facade.load(query, resultSet).exec().waitForFinished();
        resultSet->initialResultSetComplete();

        result.exec();
        QCOMPARE(result.size(), 1);

        //Enter a second result
        storage->mResults.clear();
        storage->mResults << QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id2", 0, QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor>());
        storage->mLatestRevision = 2;
        resourceAccess->emit revisionChanged(2);

        //Hack to get event loop in synclistresult to abort again
        resultSet->initialResultSetComplete();
        result.exec();

        QCOMPARE(result.size(), 2);
    }

    void testLiveQueryModify()
    {
        Akonadi2::Query query;
        query.liveQuery = true;

        auto resultSet = QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> >::create();
        auto storage = QSharedPointer<TestEntityStorage>::create("identifier");
        auto resourceAccess = QSharedPointer<TestResourceAccess>::create();
        auto entity = QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id2", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
        entity->setProperty("test", "test1");
        storage->mResults << entity;
        TestResourceFacade facade("identifier", storage, resourceAccess);

        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(resultSet->emitter());

        facade.load(query, resultSet).exec().waitForFinished();
        resultSet->initialResultSetComplete();

        result.exec();
        QCOMPARE(result.size(), 1);

        //Modify the entity again
        storage->mResults.clear();
        entity = QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id2", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
        entity->setProperty("test", "test2");
        storage->mModifications << entity;
        storage->mLatestRevision = 2;
        resourceAccess->emit revisionChanged(2);

        //Hack to get event loop in synclistresult to abort again
        resultSet->initialResultSetComplete();
        result.exec();

        QCOMPARE(result.size(), 1);
        QCOMPARE(result.first()->getProperty("test").toByteArray(), QByteArray("test2"));
    }

    void testLiveQueryRemove()
    {
        Akonadi2::Query query;
        query.liveQuery = true;

        auto resultSet = QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> >::create();
        auto storage = QSharedPointer<TestEntityStorage>::create("identifier");
        auto resourceAccess = QSharedPointer<TestResourceAccess>::create();
        auto entity = QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id2", 0, QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor>());
        storage->mResults << entity;
        TestResourceFacade facade("identifier", storage, resourceAccess);

        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(resultSet->emitter());

        facade.load(query, resultSet).exec().waitForFinished();
        resultSet->initialResultSetComplete();

        result.exec();
        QCOMPARE(result.size(), 1);

        //Remove the entity again
        storage->mResults.clear();
        storage->mRemovals << entity;
        storage->mLatestRevision = 2;
        resourceAccess->emit revisionChanged(2);

        //Hack to get event loop in synclistresult to abort again
        resultSet->initialResultSetComplete();
        result.exec();

        QCOMPARE(result.size(), 0);
    }
};

QTEST_MAIN(GenericFacadeTest)
#include "genericfacadetest.moc"
