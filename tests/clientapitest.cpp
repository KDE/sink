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
        return KAsync::null<void>();
    };
    KAsync::Job<void> modify(const T &domainObject) Q_DECL_OVERRIDE
    {
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

        resultProvider->setFetcher([query, resultProvider, this, ctx](const typename T::Ptr &parent) {
            async::run<int>([=] {
                if (parent) {
                    SinkTraceCtx(ctx) << "Running the fetcher " << parent->identifier();
                } else {
                    SinkTraceCtx(ctx) << "Running the fetcher.";
                }
                SinkTraceCtx(ctx) << "-------------------------.";
                for (const auto &res : results) {
                    // SinkTraceCtx(ctx) << "Parent filter " << query.getFilter("parent").value.toByteArray() << res->identifier() << res->getProperty("parent").toByteArray();
                    auto parentProperty = res->getProperty("parent").toByteArray();
                    if ((!parent && parentProperty.isEmpty()) || (parent && parentProperty == parent->identifier()) || query.parentProperty().isEmpty()) {
                        // SinkTraceCtx(ctx) << "Found a hit" << res->identifier();
                        resultProvider->add(res);
                    }
                }
                resultProvider->initialResultSetComplete(parent, true);
                return 0;
            }, runAsync).exec();
        });
        auto job = KAsync::syncStart<void>([query, resultProvider]() {});
        mResultProvider = resultProvider.data();
        return qMakePair(job, emitter);
    }

    QList<typename T::Ptr> results;
    Sink::ResultProviderInterface<typename T::Ptr> *mResultProvider;
    bool runAsync = false;
};


/**
 * Test of the client api implementation.
 *
 * This test works with injected dummy facades and thus doesn't write to storage.
 */
class ClientAPITest : public QObject
{
    Q_OBJECT
private slots:

    void initTestCase()
    {
        Sink::Test::initTest();
        Sink::FacadeFactory::instance().resetFactory();
        ResourceConfig::clear();
    }

    void testLoad()
    {
        auto facade = TestDummyResourceFacade<Sink::ApplicationDomain::Event>::registerFacade();
        facade->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

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
        auto facade = TestDummyResourceFacade<Sink::ApplicationDomain::Folder>::registerFacade();
        facade->results << QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        Sink::Query query;
        query.resourceFilter("dummyresource.instance1");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_COMPARE(model->rowCount(), 1);
    }

    void testModelNested()
    {
        auto facade = TestDummyResourceFacade<Sink::ApplicationDomain::Folder>::registerFacade();
        auto folder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto subfolder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "subId", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        subfolder->setParent("id");
        facade->results << folder << subfolder;
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        // Test
        Sink::Query query;
        query.resourceFilter("dummyresource.instance1");
        query.requestTree("parent");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
        model->fetchMore(model->index(0, 0));
        QTRY_VERIFY(model->data(model->index(0, 0), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(model->index(0, 0)), 1);
    }

    void testModelSignals()
    {
        auto facade = TestDummyResourceFacade<Sink::ApplicationDomain::Folder>::registerFacade();
        auto folder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto subfolder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "subId", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        subfolder->setParent("id");
        facade->results << folder << subfolder;
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        // Test
        Sink::Query query;
        query.resourceFilter("dummyresource.instance1");
        query.requestTree("parent");

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QSignalSpy spy(model.data(), SIGNAL(rowsInserted(const QModelIndex &, int, int)));
        QVERIFY(spy.isValid());
        model->fetchMore(model->index(0, 0));
        QTRY_VERIFY(spy.count() >= 1);
    }

    void testModelNestedLive()
    {
        auto facade = TestDummyResourceFacade<Sink::ApplicationDomain::Folder>::registerFacade();
        auto folder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("dummyresource.instance1", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto subfolder =
            QSharedPointer<Sink::ApplicationDomain::Folder>::create("dummyresource.instance1", "subId", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        subfolder->setParent("id");
        facade->results << folder << subfolder;
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

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
        auto facade1 = TestDummyResourceFacade<Sink::ApplicationDomain::Event>::registerFacade("dummyresource.instance1");
        facade1->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource1", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto facade2 = TestDummyResourceFacade<Sink::ApplicationDomain::Event>::registerFacade("dummyresource.instance2");
        facade2->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource2", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");
        ResourceConfig::addResource("dummyresource.instance2", "dummyresource");

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
        auto facade = TestDummyResourceFacade<Sink::ApplicationDomain::Event>::registerFacade();
        facade->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

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

    void testModelStress()
    {
        auto facade = TestDummyResourceFacade<Sink::ApplicationDomain::Folder>::registerFacade();
        facade->runAsync = true;
        for (int i = 0; i < 100; i++) {
            facade->results << QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id" + QByteArray::number(i), 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        }
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

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
