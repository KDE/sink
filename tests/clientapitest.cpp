#include <QtTest>
#include <QDebug>
#include <functional>

#include "clientapi.h"
#include "facade.h"
#include "synclistresult.h"
#include "resourceconfig.h"

class DummyResourceFacade : public Akonadi2::StoreFacade<Akonadi2::ApplicationDomain::Event>
{
public:
    ~DummyResourceFacade(){};
    KAsync::Job<void> create(const Akonadi2::ApplicationDomain::Event &domainObject) Q_DECL_OVERRIDE { return KAsync::null<void>(); };
    KAsync::Job<void> modify(const Akonadi2::ApplicationDomain::Event &domainObject) Q_DECL_OVERRIDE { return KAsync::null<void>(); };
    KAsync::Job<void> remove(const Akonadi2::ApplicationDomain::Event &domainObject) Q_DECL_OVERRIDE { return KAsync::null<void>(); };
    KAsync::Job<void> load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> > &resultProvider) Q_DECL_OVERRIDE
    {
        capturedResultProvider = resultProvider;
        return KAsync::start<void>([this, resultProvider, query]() {
            for (const auto &res : results) {
                resultProvider->add(res);
            }
        });
    }

    QList<Akonadi2::ApplicationDomain::Event::Ptr> results;
    QWeakPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> > capturedResultProvider;
};

class ClientAPITest : public QObject
{
    Q_OBJECT
private Q_SLOTS:

    static std::shared_ptr<DummyResourceFacade> registerDummyFacade()
    {
        auto facade = std::make_shared<DummyResourceFacade>();
        Akonadi2::FacadeFactory::instance().registerFacade<Akonadi2::ApplicationDomain::Event, DummyResourceFacade>("dummyresource",
            [facade](const QByteArray &instanceIdentifier) {
                return facade;
            }
        );
        return facade;
    }

    void initTestCase()
    {
        Akonadi2::FacadeFactory::instance().resetFactory();
        ResourceConfig::clear();
    }

    void testLoad()
    {
        auto facade = registerDummyFacade();
        facade->results << QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor>());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        Akonadi2::Query query;
        query.resources << "dummyresource.instance1";
        query.liveQuery = false;

        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
        result.exec();
        QCOMPARE(result.size(), 1);
    }

    //The query provider is supposed to delete itself
    void testQueryLifetime()
    {
        auto facade = registerDummyFacade();
        facade->results << QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor>());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        Akonadi2::Query query;
        query.resources << "dummyresource.instance1";
        query.liveQuery = true;

        {
            async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
            result.exec();
            QCOMPARE(result.size(), 1);
        }
        //It's running in a separate thread, so we have to wait for a moment until the query provider deletes itself.
        QTRY_VERIFY(!facade->capturedResultProvider);
    }

    void resourceManagement()
    {
        ResourceConfig::clear();
        Akonadi2::FacadeFactory::instance().registerStaticFacades();
        ResourceConfig::addResource("resourceconfig", "resourceconfig");

        Akonadi2::ApplicationDomain::AkonadiResource res;
        res.setProperty("identifier", "dummyresource.identifier1");
        res.setProperty("type", "dummyresource");

        Akonadi2::Store::create(res).exec().waitForFinished();
        {
            Akonadi2::Query query;
            query.resources << "resourceconfig";
            query.propertyFilter.insert("type", "dummyresource");
            async::SyncListResult<Akonadi2::ApplicationDomain::AkonadiResource::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::AkonadiResource>(query));
            result.exec();
            QCOMPARE(result.size(), 1);
        }

        Akonadi2::Store::remove(res).exec().waitForFinished();
        {
            Akonadi2::Query query;
            query.resources << "resourceconfig";
            query.propertyFilter.insert("type", "dummyresource");
            async::SyncListResult<Akonadi2::ApplicationDomain::AkonadiResource::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::AkonadiResource>(query));
            result.exec();
            QCOMPARE(result.size(), 0);
        }
    }

};

QTEST_MAIN(ClientAPITest)
#include "clientapitest.moc"
