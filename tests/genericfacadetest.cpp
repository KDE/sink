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

class TestResourceFacade : public Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>
{
public:
    TestResourceFacade(const QByteArray &instanceIdentifier, const QSharedPointer<EntityStorage<Akonadi2::ApplicationDomain::Event> > storage)
        : Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>(instanceIdentifier, QSharedPointer<TestEventAdaptorFactory>::create(), storage)
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

        auto storage = QSharedPointer<TestEntityStorage>::create("identifier", QSharedPointer<TestEventAdaptorFactory>::create());
        storage->mResults << Akonadi2::ApplicationDomain::Event::Ptr::create();
        TestResourceFacade facade("identifier", storage);

        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(resultSet->emitter());

        facade.load(query, resultSet).exec().waitForFinished();
        resultSet->initialResultSetComplete();

        //We have to wait for the events that deliver the results to be processed by the eventloop
        result.exec();

        QCOMPARE(result.size(), 1);
    }
};

QTEST_MAIN(GenericFacadeTest)
#include "genericfacadetest.moc"
