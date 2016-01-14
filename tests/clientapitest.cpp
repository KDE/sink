#include <QtTest>
#include <QDebug>
#include <functional>

#include "clientapi.h"
#include "facade.h"
#include "resourceconfig.h"
#include "modelresult.h"
#include "resultprovider.h"
#include "facadefactory.h"

template <typename T>
class DummyResourceFacade : public Akonadi2::StoreFacade<T>
{
public:
    static std::shared_ptr<DummyResourceFacade<T> > registerFacade(const QByteArray &instanceIdentifier = QByteArray())
    {
        static QMap<QByteArray, std::shared_ptr<DummyResourceFacade<T> > > map;
        auto facade = std::make_shared<DummyResourceFacade<T> >();
        map.insert(instanceIdentifier, facade);
        bool alwaysReturnFacade = instanceIdentifier.isEmpty();
        Akonadi2::FacadeFactory::instance().registerFacade<T, DummyResourceFacade<T> >("dummyresource",
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
    QPair<KAsync::Job<void>, typename Akonadi2::ResultEmitter<typename T::Ptr>::Ptr > load(const Akonadi2::Query &query) Q_DECL_OVERRIDE
    {
        auto resultProvider = new Akonadi2::ResultProvider<typename T::Ptr>();
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
    Akonadi2::ResultProviderInterface<typename T::Ptr> *mResultProvider;
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
        Akonadi2::Log::setDebugOutputLevel(Akonadi2::Log::Trace);
    }

    void testLoad()
    {
        auto facade = DummyResourceFacade<Akonadi2::ApplicationDomain::Event>::registerFacade();
        facade->results << QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        Akonadi2::Query query;
        query.resources << "dummyresource.instance1";
        query.liveQuery = false;

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 1);
    }

    void testLoadWithoutResource()
    {
        Akonadi2::Query query;
        query.resources << "nonexisting.resource";
        query.liveQuery = false;

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
    }

    //TODO: This test doesn't belong to this testsuite
    void resourceManagement()
    {
        ResourceConfig::clear();
        Akonadi2::FacadeFactory::instance().registerStaticFacades();

        Akonadi2::ApplicationDomain::AkonadiResource res("", "dummyresource.identifier1", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
        res.setProperty("identifier", "dummyresource.identifier1");
        res.setProperty("type", "dummyresource");

        Akonadi2::Store::create(res).exec().waitForFinished();
        {
            Akonadi2::Query query;
            query.propertyFilter.insert("type", "dummyresource");
            auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::AkonadiResource>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        }

        Akonadi2::Store::remove(res).exec().waitForFinished();
        {
            Akonadi2::Query query;
            query.propertyFilter.insert("type", "dummyresource");
            auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::AkonadiResource>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(QModelIndex()), 0);
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
        query.parentProperty = "parent";

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
        model->fetchMore(model->index(0, 0));
        QTRY_VERIFY(model->data(model->index(0, 0), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(model->index(0, 0)), 1);
    }

    void testModelSignals()
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
        query.parentProperty = "parent";

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
        QSignalSpy spy(model.data(), SIGNAL(rowsInserted(const QModelIndex &, int, int)));
        QVERIFY(spy.isValid());
        model->fetchMore(model->index(0, 0));
        QTRY_VERIFY(spy.count() >= 1);
    }

    void testModelNestedLive()
    {
        auto facade = DummyResourceFacade<Akonadi2::ApplicationDomain::Folder>::registerFacade();
        auto folder =  QSharedPointer<Akonadi2::ApplicationDomain::Folder>::create("dummyresource.instance1", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto subfolder = QSharedPointer<Akonadi2::ApplicationDomain::Folder>::create("dummyresource.instance1", "subId", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
        subfolder->setProperty("parent", "id");
        facade->results << folder << subfolder;
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");

        //Test
        Akonadi2::Query query;
        query.resources << "dummyresource.instance1";
        query.liveQuery = true;
        query.parentProperty = "parent";

        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
        QTRY_COMPARE(model->rowCount(), 1);
        model->fetchMore(model->index(0, 0));
        QTRY_COMPARE(model->rowCount(model->index(0, 0)), 1);

        auto resultProvider = facade->mResultProvider;

        //Test new toplevel folder
        {
            QSignalSpy rowsInsertedSpy(model.data(), SIGNAL(rowsInserted(const QModelIndex &, int, int)));
            auto folder2 = QSharedPointer<Akonadi2::ApplicationDomain::Folder>::create("resource", "id2", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
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
        auto facade1 = DummyResourceFacade<Akonadi2::ApplicationDomain::Event>::registerFacade("dummyresource.instance1");
        facade1->results << QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource1", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
        auto facade2 = DummyResourceFacade<Akonadi2::ApplicationDomain::Event>::registerFacade("dummyresource.instance2");
        facade2->results << QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource2", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
        ResourceConfig::addResource("dummyresource.instance1", "dummyresource");
        ResourceConfig::addResource("dummyresource.instance2", "dummyresource");

        Akonadi2::Query query;
        query.liveQuery = false;

        int childrenFetchedCount = 0;
        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
        QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [&childrenFetchedCount](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
            if (roles.contains(Akonadi2::Store::ChildrenFetchedRole)) {
                childrenFetchedCount++;
            }
        });
        QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(QModelIndex()), 2);
        //Ensure children fetched is only emitted once (when all resources are done)
        QTest::qWait(50);
        QCOMPARE(childrenFetchedCount, 1);
    }


};

QTEST_MAIN(ClientAPITest)
#include "clientapitest.moc"
