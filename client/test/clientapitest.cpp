#include <QtTest>
#include <QDebug>
#include <functional>

#include "../clientapi.h"

class DummyResourceFacade : public Akonadi2::StoreFacade<Akonadi2::Domain::Event>
{
public:
    ~DummyResourceFacade(){};
    virtual void create(const Akonadi2::Domain::Event &domainObject){};
    virtual void modify(const Akonadi2::Domain::Event &domainObject){};
    virtual void remove(const Akonadi2::Domain::Event &domainObject){};
    virtual void load(const Akonadi2::Query &query, const std::function<void(const Akonadi2::Domain::Event::Ptr &)> &resultCallback)
    {
        qDebug() << "load called";
        for(const auto &result : results) {
            resultCallback(result);
        }
    }

    QList<Akonadi2::Domain::Event::Ptr> results;
};

class ClientAPITest : public QObject
{
    Q_OBJECT
private Q_SLOTS:

    void testLoad()
    {
        DummyResourceFacade facade;
        facade.results << QSharedPointer<Akonadi2::Domain::Event>::create("resource", "id", 0);

        Akonadi2::FacadeFactory::instance().registerFacade<Akonadi2::Domain::Event, DummyResourceFacade>("dummyresource", [facade](){ return new DummyResourceFacade(facade); });

        Akonadi2::Query query;
        query.resources << "dummyresource";

        auto result = Akonadi2::Store::load<Akonadi2::Domain::Event>(query);

        QList<Akonadi2::Domain::Event::Ptr> resultSet;
        result->onAdded([&resultSet](const Akonadi2::Domain::Event::Ptr &event){ resultSet << event; qDebug() << "result added";});

        bool complete;
        result->onComplete([&complete]{ complete = true; qDebug() << "complete";});

        QTRY_VERIFY(complete);
    }

};

QTEST_MAIN(ClientAPITest)
#include "clientapitest.moc"
