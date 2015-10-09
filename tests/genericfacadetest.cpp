#include <QtTest>

#include <QString>

#include <common/facade.h>
#include <common/domainadaptor.h>
#include <common/resultprovider.h>
#include <common/synclistresult.h>

//Replace with something different
#include "event_generated.h"

class TestEventAdaptorFactory : public DomainTypeAdaptorFactory<Akonadi2::ApplicationDomain::Event, Akonadi2::ApplicationDomain::Buffer::Event, Akonadi2::ApplicationDomain::Buffer::EventBuilder>
{
public:
    TestEventAdaptorFactory()
        : DomainTypeAdaptorFactory()
    {
    }

    virtual ~TestEventAdaptorFactory() {};
};

class TestEntityStorage : public EntityStorage<Akonadi2::ApplicationDomain::Event>
{
public:
    using EntityStorage::EntityStorage;
    virtual qint64 read(const Akonadi2::Query &query, qint64 oldRevision, const QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> > &resultProvider)  Q_DECL_OVERRIDE
    {
        for (const auto &res : mResults) {
            resultProvider->add(res);
        }
        for (const auto &res : mModifications) {
            resultProvider->modify(res);
        }
        for (const auto &res : mRemovals) {
            resultProvider->remove(res);
        }
        return mLatestRevision;
    }

    QList<Akonadi2::ApplicationDomain::Event::Ptr> mResults;
    QList<Akonadi2::ApplicationDomain::Event::Ptr> mModifications;
    QList<Akonadi2::ApplicationDomain::Event::Ptr> mRemovals;
    qint64 mLatestRevision;
};

class TestResourceAccess : public Akonadi2::ResourceAccessInterface
{
    Q_OBJECT
public:
    virtual ~TestResourceAccess() {};
    KAsync::Job<void> sendCommand(int commandId) Q_DECL_OVERRIDE { return KAsync::null<void>(); }
    KAsync::Job<void> sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb) Q_DECL_OVERRIDE { return KAsync::null<void>(); }
    KAsync::Job<void> synchronizeResource(bool remoteSync, bool localSync) Q_DECL_OVERRIDE { return KAsync::null<void>(); }

public Q_SLOTS:
    void open() Q_DECL_OVERRIDE {}
    void close() Q_DECL_OVERRIDE {}
};

class TestResourceFacade : public Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>
{
public:
    TestResourceFacade(const QByteArray &instanceIdentifier, const QSharedPointer<EntityStorage<Akonadi2::ApplicationDomain::Event> > storage, const QSharedPointer<Akonadi2::ResourceAccessInterface> resourceAccess)
        : Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>(instanceIdentifier, QSharedPointer<TestEventAdaptorFactory>::create(), storage, resourceAccess)
    {

    }
    virtual ~TestResourceFacade()
    {

    }
};

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
        auto storage = QSharedPointer<TestEntityStorage>::create("identifier", QSharedPointer<TestEventAdaptorFactory>::create(), "bufferType");
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
        auto storage = QSharedPointer<TestEntityStorage>::create("identifier", QSharedPointer<TestEventAdaptorFactory>::create(), "bufferType");
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
        auto storage = QSharedPointer<TestEntityStorage>::create("identifier", QSharedPointer<TestEventAdaptorFactory>::create(), "bufferType");
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
        auto storage = QSharedPointer<TestEntityStorage>::create("identifier", QSharedPointer<TestEventAdaptorFactory>::create(), "bufferType");
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
