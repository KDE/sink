#include <QtTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "clientapi.h"

class DummyResourceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        auto factory = Akonadi2::ResourceFactory::load("org.kde.dummy");
        QVERIFY(factory);
        Akonadi2::Storage store(Akonadi2::Store::storageLocation(), "org.kde.dummy", Akonadi2::Storage::ReadWrite);
        store.removeFromDisk();
    }

    void cleanupTestCase()
    {
    }

    void testResource()
    {
        Akonadi2::Pipeline pipeline("org.kde.dummy");
        DummyResource resource;
        auto job = resource.synchronizeWithSource(&pipeline);
        auto future = job.exec();
        QTRY_VERIFY(future.isFinished());
    }

    void testSync()
    {
        Akonadi2::Query query;
        query.resources << "org.kde.dummy";

        async::SyncListResult<Akonadi2::Domain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::Domain::Event>(query));
        result.exec();
        QVERIFY(!result.isEmpty());
        auto value = result.first();
        qDebug() << value->getProperty("summary");
    }

};

QTEST_MAIN(DummyResourceTest)
#include "dummyresourcetest.moc"
