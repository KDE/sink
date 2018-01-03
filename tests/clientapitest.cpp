#include <QtTest>
#include <QDebug>
#include <functional>

#include "store.h"
#include "facade.h"
#include "resourceconfig.h"
#include "modelresult.h"
#include "resultprovider.h"
#include "facadefactory.h"
#include "test.h"
#include "asyncutils.h"

template <typename T>
struct Result {
    bool fetchedAll;
};

template <typename T>
class TestDummyResourceFacade : public Sink::StoreFacade<T>
{
public:
    static std::shared_ptr<TestDummyResourceFacade<T>> registerFacade(const QByteArray &instanceIdentifier = QByteArray())
    {
        static QMap<QByteArray, std::shared_ptr<TestDummyResourceFacade<T>>> map;
        auto facade = std::make_shared<TestDummyResourceFacade<T>>();
        map.insert(instanceIdentifier, facade);
        bool alwaysReturnFacade = instanceIdentifier.isEmpty();
        Sink::FacadeFactory::instance().registerFacade<T, TestDummyResourceFacade<T>>("dummyresource", [alwaysReturnFacade](const Sink::ResourceContext &context) {
            if (alwaysReturnFacade) {
                Q_ASSERT(map.contains(QByteArray()));
                return map.value(QByteArray());
            }
            Q_ASSERT(map.contains(context.instanceId()));
            return map.value(context.instanceId());
        });
        return facade;
    }
    ~TestDummyResourceFacade(){};
    KAsync::Job<void> create(const T &domainObject) Q_DECL_OVERRIDE
    {
        SinkLogCtx(Sink::Log::Context{"test"}) << "Create: " <<  domainObject;
        creations << domainObject;
        return KAsync::null<void>();
    };
    KAsync::Job<void> modify(const T &domainObject) Q_DECL_OVERRIDE
    {
        SinkLogCtx(Sink::Log::Context{"test"}) << "Modify: " <<  domainObject;
        modifications << domainObject;
        return KAsync::null<void>();
    };
    KAsync::Job<void> move(const T &domainObject, const QByteArray &) Q_DECL_OVERRIDE
    {
        return KAsync::null<void>();
    };
    KAsync::Job<void> copy(const T &domainObject, const QByteArray &) Q_DECL_OVERRIDE
    {
        return KAsync::null<void>();
    };
    KAsync::Job<void> remove(const T &domainObject) Q_DECL_OVERRIDE
    {
        SinkLogCtx(Sink::Log::Context{"test"}) << "Remove: " <<  domainObject;
        removals << domainObject;
        return KAsync::null<void>();
    };
    QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename T::Ptr>::Ptr> load(const Sink::Query &query, const Sink::Log::Context &ctx) Q_DECL_OVERRIDE
    {
        auto resultProvider = QSharedPointer<Sink::ResultProvider<typename T::Ptr>>::create();
        resultProvider->onDone([resultProvider,ctx]() {
            SinkTraceCtx(ctx) << "Result provider is done";
        });
        // We have to do it this way, otherwise we're not setting the fetcher right
        auto emitter = resultProvider->emitter();

        resultProvider->setFetcher([query, resultProvider, this, ctx]() {
            async::run<Result<T>>([=] {
                SinkTraceCtx(ctx) << "Running the fetcher.";
                SinkTraceCtx(ctx) << "-------------------------.";
                int count = 0;
                for (int i = offset; i < results.size(); i++) {
                    const auto res = results.at(i);
                    count++;
                    resultProvider->add(res);
                    if (query.limit()) {
                        if (count >= query.limit()) {
                            SinkTraceCtx(ctx) << "Aborting early after " << count << "results.";
                            offset = i + 1;
                            bool fetchedAll = (i + 1 >= results.size());
                            return Result<T>{fetchedAll};
                        }
                    }
                }
                return Result<T>{true};
            }, runAsync)
            .then([=] (const Result<T> &r) {
                resultProvider->initialResultSetComplete(r.fetchedAll);
            })
            .exec();
        });
        auto job = KAsync::start([query, resultProvider]() {});
        mResultProvider = resultProvider.data();
        return qMakePair(job, emitter);
    }

    QList<typename T::Ptr> results;
    Sink::ResultProviderInterface<typename T::Ptr> *mResultProvider;
    bool runAsync = false;
    int offset = 0;
    QList<T> creations;
    QList<T> modifications;
    QList<T> removals;
};


/**
 * Test of the client api implementation.
 *
 * This test works with injected dummy facades and thus doesn't write to storage.
 */
class ClientAPITest : public QObject
{
    Q_OBJECT

    template<typename T>
    std::shared_ptr<TestDummyResourceFacade<T> >  setupFacade(const QByteArray &identifier)
    {
        auto facade = TestDummyResourceFacade<T>::registerFacade(identifier);
        ResourceConfig::addResource(identifier, "dummyresource");
        QMap<QByteArray, QVariant> config = ResourceConfig::getConfiguration(identifier);
        config.insert(Sink::ApplicationDomain::SinkResource::Capabilities::name, QVariant::fromValue(QByteArrayList() << Sink::ApplicationDomain::getTypeName<T>()));
        ResourceConfig::configureResource(identifier, config);
        return facade;
    }

private slots:

    void initTestCase()
    {
        Sink::Test::initTest();
        Sink::FacadeFactory::instance().resetFactory();
        ResourceConfig::clear();
    }

    void testLoad()
    {
        auto facade = setupFacade<Sink::ApplicationDomain::Event>("dummyresource.instance1");
        facade->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());

        Sink::Query query;
        query.resourceFilter("dummyresource.instance1");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 1);
    }

    void testLoadWithoutResource()
    {
        Sink::Query query;
        query.resourceFilter("nonexisting.resource");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
    }

    void testModelSingle()
    {
        auto facade = setupFacade<Sink::ApplicationDomain::Folder>("dummyresource.instance1");
        facade->results << QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());

        Sink::Query query;
        query.resourceFilter("dummyresource.instance1");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_COMPARE(model->rowCount(), 1);
    }

    void testModelSignals()
    {
        auto facade = setupFacade<Sink::ApplicationDomain::Folder>("dummyresource.instance1");
        facade->runAsync = true;
        auto folder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto subfolder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "subId", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        subfolder->setParent("id");
        facade->results << folder << subfolder;

        // Test
        Sink::Query query;
        query.resourceFilter("dummyresource.instance1");
        query.requestTree("parent");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QSignalSpy spy(model.data(), SIGNAL(rowsInserted(const QModelIndex &, int, int)));
        QVERIFY(spy.isValid());
        QTRY_VERIFY(spy.count() == 2);
    }

    void testModelNested()
    {
        auto facade = setupFacade<Sink::ApplicationDomain::Folder>("dummyresource.instance1");
        facade->runAsync = true;
        auto folder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto subfolder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "subId", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        subfolder->setParent("id");
        facade->results << folder << subfolder;

        // Test
        Sink::Query query;
        query.resourceFilter("dummyresource.instance1");
        query.requestTree("parent");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, [&] (const QModelIndex &parent, int first, int last) {
            for (int row = first; row <= last; row++) {
                auto index = model->index(row, 0, parent);
                QVERIFY(index.isValid());
                QVERIFY(index.data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Folder::Ptr>());
            }
        });
        QTRY_COMPARE(model->rowCount(), 1);
        QTRY_COMPARE(model->rowCount(model->index(0, 0)), 1);
    }

    void testModelNestedReverse()
    {
        auto facade = setupFacade<Sink::ApplicationDomain::Folder>("dummyresource.instance1");
        facade->runAsync = true;
        auto folder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto subfolder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "subId", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        subfolder->setParent("id");
        facade->results << subfolder << folder;

        // Test
        Sink::Query query;
        query.resourceFilter("dummyresource.instance1");
        query.requestTree("parent");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, [&] (const QModelIndex &parent, int first, int last) {
            for (int row = first; row <= last; row++) {
                auto index = model->index(row, 0, parent);
                QVERIFY(index.isValid());
                QVERIFY(index.data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Folder::Ptr>());
            }
        });
        QTRY_COMPARE(model->rowCount(), 1);
        QVERIFY(model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Folder::Ptr>());
        QTRY_COMPARE(model->rowCount(model->index(0, 0)), 1);
    }

    void testModelNestedLive()
    {
        auto facade = setupFacade<Sink::ApplicationDomain::Folder>("dummyresource.instance1");
        auto folder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("dummyresource.instance1", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto subfolder =
            QSharedPointer<Sink::ApplicationDomain::Folder>::create("dummyresource.instance1", "subId", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        subfolder->setParent("id");
        facade->results << folder << subfolder;

        // Test
        Sink::Query query;
        query.resourceFilter("dummyresource.instance1");
        query.setFlags(Sink::Query::LiveQuery);
        query.requestTree("parent");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_COMPARE(model->rowCount(), 1);
        model->fetchMore(model->index(0, 0));
        QTRY_COMPARE(model->rowCount(model->index(0, 0)), 1);

        auto resultProvider = facade->mResultProvider;

        // Test new toplevel folder
        {
            QSignalSpy rowsInsertedSpy(model.data(), SIGNAL(rowsInserted(const QModelIndex &, int, int)));
            auto folder2 = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id2", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
            resultProvider->add(folder2);
            QTRY_COMPARE(model->rowCount(), 2);
            QTRY_COMPARE(rowsInsertedSpy.count(), 1);
            QCOMPARE(rowsInsertedSpy.at(0).at(0).value<QModelIndex>(), QModelIndex());
        }

        // Test changed name
        {
            QSignalSpy dataChanged(model.data(), SIGNAL(dataChanged(const QModelIndex &, const QModelIndex &, const QVector<int> &)));
            folder->setProperty("subject", "modifiedSubject");
            resultProvider->modify(folder);
            QTRY_COMPARE(model->rowCount(), 2);
            QTRY_COMPARE(dataChanged.count(), 1);
        }

        // Test removal
        {
            QSignalSpy rowsRemovedSpy(model.data(), SIGNAL(rowsRemoved(const QModelIndex &, int, int)));
            folder->setProperty("subject", "modifiedSubject");
            resultProvider->remove(subfolder);
            QTRY_COMPARE(model->rowCount(model->index(0, 0)), 0);
            QTRY_COMPARE(rowsRemovedSpy.count(), 1);
        }

        // TODO: A modification can also be a move
    }

    void testLoadMultiResource()
    {
        auto facade1 = setupFacade<Sink::ApplicationDomain::Event>("dummyresource.instance1");
        facade1->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource1", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto facade2 = setupFacade<Sink::ApplicationDomain::Event>("dummyresource.instance2");
        facade2->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource2", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());

        Sink::Query query;

        int childrenFetchedCount = 0;
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
        QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [&childrenFetchedCount](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
            if (roles.contains(Sink::Store::ChildrenFetchedRole)) {
                childrenFetchedCount++;
            }
        });
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 2);
        // Ensure children fetched is only emitted once (when all resources are done)
        QTest::qWait(50);
        QVERIFY(childrenFetchedCount <= 1);
    }

    void testImperativeLoad()
    {
        auto facade = setupFacade<Sink::ApplicationDomain::Event>("dummyresource.instance1");
        facade->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());

        Sink::Query query;
        query.resourceFilter("dummyresource.instance1");

        bool gotValue = false;
        auto result = Sink::Store::fetchOne<Sink::ApplicationDomain::Event>(query)
                          .then([&gotValue](const Sink::ApplicationDomain::Event &event) { gotValue = true; })
                          .exec();
        result.waitForFinished();
        QVERIFY(!result.errorCode());
        QVERIFY(gotValue);
    }

    void testMultiresourceIncrementalLoad()
    {
        auto facade1 = setupFacade<Sink::ApplicationDomain::Event>("dummyresource.instance1");
        for (int i = 0; i < 4; i++) {
            facade1->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource1", "id" + QByteArray::number(i), 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        }

        auto facade2 = setupFacade<Sink::ApplicationDomain::Event>("dummyresource.instance2");
        for (int i = 0; i < 6; i++) {
            facade2->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource2", "id" + QByteArray::number(i), 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        }

        Sink::Query query;
        query.limit(2);

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 4);

        //Try to fetch another round
        QVERIFY(model->canFetchMore(QModelIndex()));
        model->fetchMore(QModelIndex());
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 8);

        //Try to fetch the last round
        QVERIFY(model->canFetchMore(QModelIndex()));
        model->fetchMore(QModelIndex());
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 10);

        QVERIFY(!model->canFetchMore(QModelIndex()));
    }

    void testCreateModifyDelete()
    {
        auto facade = setupFacade<Sink::ApplicationDomain::Event>("dummyresource.instance1");

        auto event = Sink::ApplicationDomain::Event::createEntity<Sink::ApplicationDomain::Event>("dummyresource.instance1");
        Sink::Store::create(event).exec().waitForFinished();
        QCOMPARE(facade->creations.size(), 1);
        //Modify something so the mdofication won't be dropped
        event.setSummary("foobar");
        Sink::Store::modify(event).exec().waitForFinished();
        QCOMPARE(facade->modifications.size(), 1);
        Sink::Store::remove(event).exec().waitForFinished();
        QCOMPARE(facade->removals.size(), 1);

    }
    void testMultiModify()
    {
        auto facade = setupFacade<Sink::ApplicationDomain::Event>("dummyresource.instance1");
        facade->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("dummyresource.instance1", "id1", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        facade->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("dummyresource.instance1", "id2", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());

        Sink::Query query;
        query.resourceFilter("dummyresource.instance1");

        auto event = Sink::ApplicationDomain::Event::createEntity<Sink::ApplicationDomain::Event>("dummyresource.instance1");
        event.setUid("modifiedUid");
        Sink::Store::modify(query, event).exec().waitForFinished();
        QCOMPARE(facade->modifications.size(), 2);
        for (const auto &m : facade->modifications) {
            QCOMPARE(m.getUid(), QString("modifiedUid"));
        }
    }

    void testModelStress()
    {
        auto facade = setupFacade<Sink::ApplicationDomain::Folder>("dummyresource.instance1");
        facade->runAsync = true;
        for (int i = 0; i < 100; i++) {
            facade->results << QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id" + QByteArray::number(i), 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        }

        Sink::Query query;
        query.resourceFilter("dummyresource.instance1");

        for (int i = 0; i < 100; i++) {
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
            model->fetchMore(QModelIndex());
            QTest::qWait(1);
        }
        QTest::qWait(100);
    }

};

QTEST_MAIN(ClientAPITest)
#include "clientapitest.moc"
