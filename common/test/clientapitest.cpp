#include <QtTest>
#include <QDebug>
#include <functional>

#include "../clientapi.h"

class DummyResourceFacade : public Akonadi2::StoreFacade<Akonadi2::ApplicationDomain::Event>
{
public:
    ~DummyResourceFacade(){};
    virtual Async::Job<void> create(const Akonadi2::ApplicationDomain::Event &domainObject){ return Async::null<void>(); };
    virtual Async::Job<void> modify(const Akonadi2::ApplicationDomain::Event &domainObject){ return Async::null<void>(); };
    virtual Async::Job<void> remove(const Akonadi2::ApplicationDomain::Event &domainObject){ return Async::null<void>(); };
    virtual Async::Job<void> load(const Akonadi2::Query &query, const std::function<void(const Akonadi2::ApplicationDomain::Event::Ptr &)> &resultCallback)
    {
        return Async::start<void>([this, resultCallback](Async::Future<void> &future) {
            qDebug() << "load called";
            for(const auto &result : results) {
                resultCallback(result);
            }
            future.setFinished();
        });
    }

    QList<Akonadi2::ApplicationDomain::Event::Ptr> results;
};

class ClientAPITest : public QObject
{
    Q_OBJECT
private Q_SLOTS:

    void testLoad()
    {
        DummyResourceFacade facade;
        facade.results << QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("resource", "id", 0, QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor>());

        Akonadi2::FacadeFactory::instance().registerFacade<Akonadi2::ApplicationDomain::Event, DummyResourceFacade>("dummyresource", [facade](){ return new DummyResourceFacade(facade); });

        Akonadi2::Query query;
        query.resources << "dummyresource";

        async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
        result.exec();
        QCOMPARE(result.size(), 1);
    }

};

QTEST_MAIN(ClientAPITest)
#include "clientapitest.moc"
