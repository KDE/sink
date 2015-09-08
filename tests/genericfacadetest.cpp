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
    virtual void read(const Akonadi2::Query &query, const QPair<qint64, qint64> &revisionRange, const QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> > &resultProvider)  Q_DECL_OVERRIDE
    {
        for (const auto &res : mResults) {
            resultProvider->add(res);
        }
    }

    QList<Akonadi2::ApplicationDomain::Event::Ptr> mResults;
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
        resourceAccess->emit revisionChanged(2);

        //Hack to get event loop in synclistresult to abort again
        resultSet->initialResultSetComplete();
        result.exec();

        // QTRY_COMPARE(result.size(), 2);
        QCOMPARE(result.size(), 2);
    }
};

QTEST_MAIN(GenericFacadeTest)
#include "genericfacadetest.moc"
