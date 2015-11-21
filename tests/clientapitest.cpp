#include <QtTest>
#include <QDebug>
#include <functional>

#include "clientapi.h"
#include "facade.h"
#include "synclistresult.h"
#include "resourceconfig.h"
#include "modelresult.h"
#include "resultprovider.h"
#include "facadefactory.h"

template <typename T>
class DummyResourceFacade : public Akonadi2::StoreFacade<T>
{
public:
    static std::shared_ptr<DummyResourceFacade<T> > registerFacade()
    {
        auto facade = std::make_shared<DummyResourceFacade<T> >();
        Akonadi2::FacadeFactory::instance().registerFacade<T, DummyResourceFacade<T> >("dummyresource",
            [facade](const QByteArray &instanceIdentifier) {
                return facade;
            }
        );
        return facade;
    }
    ~DummyResourceFacade(){};
    KAsync::Job<void> create(const T &domainObject) Q_DECL_OVERRIDE { return KAsync::null<void>(); };
    KAsync::Job<void> modify(const T &domainObject) Q_DECL_OVERRIDE { return KAsync::null<void>(); };
    KAsync::Job<void> remove(const T &domainObject) Q_DECL_OVERRIDE { return KAsync::null<void>(); };
    KAsync::Job<void> load(const Akonadi2::Query &query, Akonadi2::ResultProviderInterface<typename T::Ptr> &resultProvider) Q_DECL_OVERRIDE
    {
        capturedResultProvider = &resultProvider;
        resultProvider.setFetcher([query, &resultProvider, this](const typename T::Ptr &) {
             for (const auto &res : results) {
                qDebug() << "Parent filter " << query.propertyFilter.value("parent").toByteArray() << res->identifier();
                if (!query.propertyFilter.contains("parent") || query.propertyFilter.value("parent").toByteArray() == res->getProperty("parent").toByteArray()) {
                    resultProvider.add(res);
                }
            }
        });
        return KAsync::null<void>();
    }

    QList<typename T::Ptr> results;
    Akonadi2::ResultProviderInterface<typename T::Ptr> *capturedResultProvider;
};


/**
 * Test of the client api implementation.
 * 
 * This test works with injected dummy facades and thus doesn't write to storage.
 */
class ClientAPITest : public QObject
{
    Q_OBJECT
private Q_SLOTS:

    void initTestCase()
    {
        Akonadi2::FacadeFactory::instance().resetFactory();
        ResourceConfig::clear();
    }

    void testLoad()
    {
        auto facade = DummyResourceFacade<Akonadi2::ApplicationDomain::Event>::registerFacade();
        facade->results << QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
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
        auto facade = DummyResourceFacade<Akonadi2::ApplicationDomain::Event>::registerFacade();
        facade->results << QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
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
        // QTRY_VERIFY(!facade->capturedResultProvider);
    }

    //TODO: This test doesn't belong to this testsuite
    void resourceManagement()
    {
        ResourceConfig::clear();
        Akonadi2::FacadeFactory::instance().registerStaticFacades();

        Akonadi2::ApplicationDomain::AkonadiResource res;
        res.setProperty("identifier", "dummyresource.identifier1");
        res.setProperty("type", "dummyresource");

        Akonadi2::Store::create(res).exec().waitForFinished();
        {
            Akonadi2::Query query;
            query.propertyFilter.insert("type", "dummyresource");
            async::SyncListResult<Akonadi2::ApplicationDomain::AkonadiResource::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::AkonadiResource>(query));
            result.exec();
            QCOMPARE(result.size(), 1);
        }

        Akonadi2::Store::remove(res).exec().waitForFinished();
        {
            Akonadi2::Query query;
            query.propertyFilter.insert("type", "dummyresource");
            async::SyncListResult<Akonadi2::ApplicationDomain::AkonadiResource::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::AkonadiResource>(query));
            result.exec();
            QCOMPARE(result.size(), 0);
        }
    }

    void testModelSingle()
    {
        auto facade = DummyResourceFacade<Akonadi2::ApplicationDomain::Folder>::registerFacade();
        facade->results << QSharedPointer<Akonadi2::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        Akonadi2::Query query;
        query.resources << "dummyresource.instance1";
        query.liveQuery = false;

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
        model->fetchMore(QModelIndex());
        QTRY_COMPARE(model->rowCount(), 1);
    }

    void testModelNested()
    {
        auto facade = DummyResourceFacade<Akonadi2::ApplicationDomain::Folder>::registerFacade();
        auto folder =  QSharedPointer<Akonadi2::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto subfolder = QSharedPointer<Akonadi2::ApplicationDomain::Folder>::create("resource", "subId", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
        subfolder->setProperty("parent", "id");
        facade->results << folder << subfolder;
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        //Test
        Akonadi2::Query query;
        query.resources << "dummyresource.instance1";
        query.liveQuery = false;

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
        model->fetchMore(QModelIndex());
        QTRY_COMPARE(model->rowCount(), 1);
        model->fetchMore(model->index(0, 0));
        QTRY_COMPARE(model->rowCount(model->index(0, 0)), 1);
    }

    // void testModelNestedLive()
    // {
    //     auto facade = DummyResourceFacade<Akonadi2::ApplicationDomain::Folder>::registerFacade();
    //     auto folder =  QSharedPointer<Akonadi2::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
    //     auto subfolder = QSharedPointer<Akonadi2::ApplicationDomain::Folder>::create("resource", "subId", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
    //     subfolder->setProperty("parent", "id");
    //     facade->results << folder << subfolder;
    //     ResourceConfig::addResource("dummyresource.instance1", "dummyresource");
    //
    //     //Test
    //     Akonadi2::Query query;
    //     query.resources << "dummyresource.instance1";
    //     query.liveQuery = true
    //
    //     auto model = new ModelResult<Akonadi2::ApplicationDomain::Folder>(query, QList<QByteArray>() << "summary" << "uid");
    //     model->fetchMore(QModelIndex());
    //     QTRY_COMPARE(model->rowCount(), 1);
    //     model->fetchMore(model->index(0, 0));
    //     QTRY_COMPARE(model->rowCount(model->index(0, 0)), 1);
    //
    //     auto resultProvider = facade->capturedResultProvider.toStrongRef();
    //
    //     //A modification can also be a move
    //     // resultProvider->modify();
    //
    //     // resultProvider->remove();
    // }


};

QTEST_MAIN(ClientAPITest)
#include "clientapitest.moc"
