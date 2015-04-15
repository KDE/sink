#include <QtTest>
#include <QDebug>
#include <functional>

#include "clientapi.h"
#include "facade.h"
#include "synclistresult.h"

class RevisionNotifier : public QObject
{
    Q_OBJECT
public:
    RevisionNotifier() : QObject() {};
    void notify(qint64 revision)
    {
        emit revisionChanged(revision);
    }

Q_SIGNALS:
    void revisionChanged(qint64);
};

class DummyResourceFacade : public Akonadi2::StoreFacade<Akonadi2::ApplicationDomain::Event>
{
public:
    ~DummyResourceFacade(){};
    virtual Async::Job<void> create(const Akonadi2::ApplicationDomain::Event &domainObject){ return Async::null<void>(); };
    virtual Async::Job<void> modify(const Akonadi2::ApplicationDomain::Event &domainObject){ return Async::null<void>(); };
    virtual Async::Job<void> remove(const Akonadi2::ApplicationDomain::Event &domainObject){ return Async::null<void>(); };
    virtual Async::Job<qint64> load(const Akonadi2::Query &query, const std::function<void(const Akonadi2::ApplicationDomain::Event::Ptr &)> &resultCallback)
    {
        return Async::start<qint64>([this, resultCallback](Async::Future<qint64> &future) {
            qDebug() << "load called";
            for(const auto &result : results) {
                resultCallback(result);
            }
            future.setValue(0);
            future.setFinished();
        });
    }

    Async::Job<void> load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> > &resultProvider)
    {
        auto runner = QSharedPointer<QueryRunner>::create(query);
        //The runner only lives as long as the resultProvider
        resultProvider->setQueryRunner(runner);
        runner->setQuery([this, resultProvider, query](qint64 oldRevision, qint64 newRevision) -> Async::Job<qint64> {
            qDebug() << "Creating query for revisions: " << oldRevision << newRevision;
            return Async::start<qint64>([this, resultProvider, query](Async::Future<qint64> &future) {
                //TODO only emit changes and don't replace everything
                resultProvider->clear();
                //rerun query
                std::function<void(const Akonadi2::ApplicationDomain::Event::Ptr &)> addCallback = std::bind(&Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr>::add, resultProvider, std::placeholders::_1);
                load(query, addCallback).then<void, qint64>([resultProvider, &future](qint64 queriedRevision) {
                    //TODO set revision in result provider?
                    //TODO update all existing results with new revision
                    resultProvider->complete();
                    future.setValue(queriedRevision);
                    future.setFinished();
                }).exec();
            });
        });

        //Ensure the notification is emitted in the right thread
        //Otherwise we get crashes as we call revisionChanged from the test.
        if (!notifier) {
            notifier.reset(new RevisionNotifier);
        }

        //TODO somehow disconnect as resultNotifier is destroyed. Otherwise we keep the runner alive forever.
        if (query.liveQuery) {
            QObject::connect(notifier.data(), &RevisionNotifier::revisionChanged, [runner](qint64 newRevision) {
                runner->revisionChanged(newRevision);
            });
        }

        return Async::start<void>([runner](Async::Future<void> &future) {
            runner->run().then<void>([&future]() {
                //TODO if not live query, destroy runner.
                future.setFinished();
            }).exec();
        });
    }

    QList<Akonadi2::ApplicationDomain::Event::Ptr> results;
    QSharedPointer<RevisionNotifier> notifier;
};

class ClientAPITest : public QObject
{
    Q_OBJECT
private Q_SLOTS:

    void initTestCase()
    {
        Akonadi2::FacadeFactory::instance().resetFactory();
    }

    void testLoad()
    {
        DummyResourceFacade facade;
        facade.results << QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor>());

        Akonadi2::FacadeFactory::instance().registerFacade<Akonadi2::ApplicationDomain::Event, DummyResourceFacade>("dummyresource",
            [&facade](bool &externallyManaged) {
                externallyManaged = true;
                return &facade;
            }
        );

        Akonadi2::Query query;
        query.resources << "dummyresource";
        query.liveQuery = false;

        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
        result.exec();
        QCOMPARE(result.size(), 1);
    }

    void testLiveQuery()
    {
        DummyResourceFacade facade;
        facade.results << QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor>());

        Akonadi2::FacadeFactory::instance().registerFacade<Akonadi2::ApplicationDomain::Event, DummyResourceFacade>("dummyresource",
            [&facade](bool &externallManage){
                externallManage = true;
                return &facade;
            }
        );

        Akonadi2::Query query;
        query.resources << "dummyresource";
        query.liveQuery = true;

        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
        result.exec();
        QCOMPARE(result.size(), 1);

        //Enter a second result
        facade.results << QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id2", 0, QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor>());
        qWarning() << &facade;
        QVERIFY(facade.notifier);
        facade.notifier->revisionChanged(2);
        QTRY_COMPARE(result.size(), 2);
    }

};

QTEST_MAIN(ClientAPITest)
#include "clientapitest.moc"
