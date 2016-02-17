#include <QtTest>
#include <QDebug>
#include <functional>

#include "store.h"
#include "facade.h"
#include "resourceconfig.h"
#include "modelresult.h"
#include "resultprovider.h"
#include "facadefactory.h"

template <typename T>
class DummyResourceFacade : public Sink::StoreFacade<T>
{
public:
    static std::shared_ptr<DummyResourceFacade<T> > registerFacade(const QByteArray &instanceIdentifier = QByteArray())
    {
        static QMap<QByteArray, std::shared_ptr<DummyResourceFacade<T> > > map;
        auto facade = std::make_shared<DummyResourceFacade<T> >();
        map.insert(instanceIdentifier, facade);
        bool alwaysReturnFacade = instanceIdentifier.isEmpty();
        Sink::FacadeFactory::instance().registerFacade<T, DummyResourceFacade<T> >("dummyresource",
            [alwaysReturnFacade](const QByteArray &instanceIdentifier) {
                if (alwaysReturnFacade) {
                    return map.value(QByteArray());
                }
                return map.value(instanceIdentifier);
            }
        );
        return facade;
    }
    ~DummyResourceFacade(){};
    KAsync::Job<void> create(const T &domainObject) Q_DECL_OVERRIDE { return KAsync::null<void>(); };
    KAsync::Job<void> modify(const T &domainObject) Q_DECL_OVERRIDE { return KAsync::null<void>(); };
    KAsync::Job<void> remove(const T &domainObject) Q_DECL_OVERRIDE { return KAsync::null<void>(); };
    QPair<KAsync::Job<void>, typename Sink::ResultEmitter<typename T::Ptr>::Ptr > load(const Sink::Query &query) Q_DECL_OVERRIDE
    {
        auto resultProvider = new Sink::ResultProvider<typename T::Ptr>();
        resultProvider->onDone([resultProvider]() {
            Trace() << "Result provider is done";
            delete resultProvider;
        });
        //We have to do it this way, otherwise we're not setting the fetcher right
        auto emitter = resultProvider->emitter();

        resultProvider->setFetcher([query, resultProvider, this](const typename T::Ptr &parent) {
            if (parent) {
                Trace() << "Running the fetcher " << parent->identifier();
            } else {
                Trace() << "Running the fetcher.";
            }
            Trace() << "-------------------------.";
            for (const auto &res : results) {
                qDebug() << "Parent filter " << query.propertyFilter.value("parent").toByteArray() << res->identifier() << res->getProperty("parent").toByteArray();
                auto parentProperty = res->getProperty("parent").toByteArray();
                if ((!parent && parentProperty.isEmpty()) || (parent && parentProperty == parent->identifier()) || query.parentProperty.isEmpty()) {
                    qDebug() << "Found a hit" << res->identifier();
                    resultProvider->add(res);
                }
            }
            resultProvider->initialResultSetComplete(parent);
        });
        auto job = KAsync::start<void>([query, resultProvider]() {
        });
        mResultProvider = resultProvider;
        return qMakePair(job, emitter);
    }

    QList<typename T::Ptr> results;
    Sink::ResultProviderInterface<typename T::Ptr> *mResultProvider;
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
        Sink::FacadeFactory::instance().resetFactory();
        ResourceConfig::clear();
        Sink::Log::setDebugOutputLevel(Sink::Log::Trace);
    }

    void testLoad()
    {
        auto facade = DummyResourceFacade<Sink::ApplicationDomain::Event>::registerFacade();
        facade->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        Sink::Query query;
        query.resources << "dummyresource.instance1";
        query.liveQuery = false;

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 1);
    }

    void testLoadWithoutResource()
    {
        Sink::Query query;
        query.resources << "nonexisting.resource";
        query.liveQuery = false;

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
    }

    //TODO: This test doesn't belong to this testsuite
    void resourceManagement()
    {
        ResourceConfig::clear();
        Sink::FacadeFactory::instance().registerStaticFacades();

        Sink::ApplicationDomain::SinkResource res("", "dummyresource.identifier1", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        res.setProperty("identifier", "dummyresource.identifier1");
        res.setProperty("type", "dummyresource");

        Sink::Store::create(res).exec().waitForFinished();
        {
            Sink::Query query;
            query.propertyFilter.insert("type", "dummyresource");
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::SinkResource>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        }

        Sink::Store::remove(res).exec().waitForFinished();
        {
            Sink::Query query;
            query.propertyFilter.insert("type", "dummyresource");
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::SinkResource>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(QModelIndex()), 0);
        }
    }

    void testModelSingle()
    {
        auto facade = DummyResourceFacade<Sink::ApplicationDomain::Folder>::registerFacade();
        facade->results << QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        Sink::Query query;
        query.resources << "dummyresource.instance1";
        query.liveQuery = false;

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_COMPARE(model->rowCount(), 1);
    }

    void testModelNested()
    {
        auto facade = DummyResourceFacade<Sink::ApplicationDomain::Folder>::registerFacade();
        auto folder =  QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto subfolder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "subId", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        subfolder->setProperty("parent", "id");
        facade->results << folder << subfolder;
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        //Test
        Sink::Query query;
        query.resources << "dummyresource.instance1";
        query.liveQuery = false;
        query.parentProperty = "parent";

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
        model->fetchMore(model->index(0, 0));
        QTRY_VERIFY(model->data(model->index(0, 0), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(model->index(0, 0)), 1);
    }

    void testModelSignals()
    {
        auto facade = DummyResourceFacade<Sink::ApplicationDomain::Folder>::registerFacade();
        auto folder =  QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto subfolder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "subId", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        subfolder->setProperty("parent", "id");
        facade->results << folder << subfolder;
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        //Test
        Sink::Query query;
        query.resources << "dummyresource.instance1";
        query.liveQuery = false;
        query.parentProperty = "parent";

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QSignalSpy spy(model.data(), SIGNAL(rowsInserted(const QModelIndex &, int, int)));
        QVERIFY(spy.isValid());
        model->fetchMore(model->index(0, 0));
        QTRY_VERIFY(spy.count() >= 1);
    }

    void testModelNestedLive()
    {
        auto facade = DummyResourceFacade<Sink::ApplicationDomain::Folder>::registerFacade();
        auto folder =  QSharedPointer<Sink::ApplicationDomain::Folder>::create("dummyresource.instance1", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto subfolder = QSharedPointer<Sink::ApplicationDomain::Folder>::create("dummyresource.instance1", "subId", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        subfolder->setProperty("parent", "id");
        facade->results << folder << subfolder;
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        //Test
        Sink::Query query;
        query.resources << "dummyresource.instance1";
        query.liveQuery = true;
        query.parentProperty = "parent";

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_COMPARE(model->rowCount(), 1);
        model->fetchMore(model->index(0, 0));
        QTRY_COMPARE(model->rowCount(model->index(0, 0)), 1);

        auto resultProvider = facade->mResultProvider;

        //Test new toplevel folder
        {
            QSignalSpy rowsInsertedSpy(model.data(), SIGNAL(rowsInserted(const QModelIndex &, int, int)));
            auto folder2 = QSharedPointer<Sink::ApplicationDomain::Folder>::create("resource", "id2", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
            resultProvider->add(folder2);
            QTRY_COMPARE(model->rowCount(), 2);
            QTRY_COMPARE(rowsInsertedSpy.count(), 1);
            QCOMPARE(rowsInsertedSpy.at(0).at(0).value<QModelIndex>(), QModelIndex());
        }

        //Test changed name
        {
            QSignalSpy dataChanged(model.data(), SIGNAL(dataChanged(const QModelIndex &, const QModelIndex &, const QVector<int> &)));
            folder->setProperty("subject", "modifiedSubject");
            resultProvider->modify(folder);
            QTRY_COMPARE(model->rowCount(), 2);
            QTRY_COMPARE(dataChanged.count(), 1);
        }

        //Test removal
        {
            QSignalSpy rowsRemovedSpy(model.data(), SIGNAL(rowsRemoved(const QModelIndex &, int, int)));
            folder->setProperty("subject", "modifiedSubject");
            resultProvider->remove(subfolder);
            QTRY_COMPARE(model->rowCount(model->index(0, 0)), 0);
            QTRY_COMPARE(rowsRemovedSpy.count(), 1);
        }

        //TODO: A modification can also be a move
    }

    void testLoadMultiResource()
    {
        auto facade1 = DummyResourceFacade<Sink::ApplicationDomain::Event>::registerFacade("dummyresource.instance1");
        facade1->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource1", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto facade2 = DummyResourceFacade<Sink::ApplicationDomain::Event>::registerFacade("dummyresource.instance2");
        facade2->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource2", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");
        ResourceConfig::addResource("dummyresource.instance2", "dummyresource");

        Sink::Query query;
        query.liveQuery = false;

        int childrenFetchedCount = 0;
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
        QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [&childrenFetchedCount](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
            if (roles.contains(Sink::Store::ChildrenFetchedRole)) {
                childrenFetchedCount++;
            }
        });
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 2);
        //Ensure children fetched is only emitted once (when all resources are done)
        QTest::qWait(50);
        QCOMPARE(childrenFetchedCount, 1);
    }

    void testImperativeLoad()
    {
        auto facade = DummyResourceFacade<Sink::ApplicationDomain::Event>::registerFacade();
        facade->results << QSharedPointer<Sink::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        Sink::Query query;
        query.resources << "dummyresource.instance1";
        query.liveQuery = false;

        bool gotValue = false;
        auto result = Sink::Store::fetchOne<Sink::ApplicationDomain::Event>(query)
            .then<void, Sink::ApplicationDomain::Event>([&gotValue](const Sink::ApplicationDomain::Event &event) {
                gotValue = true;
            }).exec();
        result.waitForFinished();
        QVERIFY(!result.errorCode());
        QVERIFY(gotValue);
    }


};

QTEST_MAIN(ClientAPITest)
#include "clientapitest.moc"
