#include <QtTest>

#include <QString>

#include "common/resource.h"
#include "clientapi.h"

class DummyResourceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        auto factory = Akonadi2::ResourceFactory::load("org.kde.dummy");
        QVERIFY(factory);
    }

    void cleanupTestCase()
    {
    }

    void testSync()
    {
        Akonadi2::Query query;
        query.resources << "org.kde.dummy";

        async::SyncListResult<Akonadi2::Domain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::Domain::Event>(query));
        result.exec();
        QVERIFY(!result.isEmpty());
    }

};

QTEST_MAIN(DummyResourceTest)
#include "dummyresourcetest.moc"
