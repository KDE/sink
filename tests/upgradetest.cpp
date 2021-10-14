#include <QTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "store.h"
#include "resourceconfig.h"
#include "resourcecontrol.h"
#include "log.h"
#include "test.h"
#include "definitions.h"
#include "storage.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

class UpgradeTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Sink::Test::initTest();
        auto factory = Sink::ResourceFactory::load("sink.dummy");
        QVERIFY(factory);
        ::DummyResource::removeFromDisk("sink.dummy.instance1");
        ResourceConfig::addResource("sink.dummy.instance1", "sink.dummy");
    }

    void init()
    {
    }

    void cleanup()
    {
        VERIFYEXEC(Sink::Store::removeDataFromDisk(QByteArray("sink.dummy.instance1")));
    }

    void noUpgradeOnNoDb()
    {
        auto upgradeJob = Sink::Store::upgrade()
            .then([](const Sink::Store::UpgradeResult &result) {
                ASYNCVERIFY(!result.upgradeExecuted);
                return KAsync::null();
            });
        VERIFYEXEC(upgradeJob);
    }

    void noUpgradeOnCurrentDb()
    {
        Event event("sink.dummy.instance1");
        event.setProperty("uid", "testuid");
        event.setProperty("summary", "summaryValue");
        Sink::Store::create<Event>(event).exec().waitForFinished();

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        auto upgradeJob = Sink::Store::upgrade()
            .then([](const Sink::Store::UpgradeResult &result) {
                ASYNCVERIFY(!result.upgradeExecuted);
                return KAsync::null();
            });
        VERIFYEXEC(upgradeJob);
    }

    void upgradeFromOldDb()
    {
        Event event("sink.dummy.instance1");
        event.setProperty("uid", "testuid");
        event.setProperty("summary", "summaryValue");
        Sink::Store::create<Event>(event).exec().waitForFinished();

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        //force the db to an old version.
        {
            Sink::Storage::DataStore store(Sink::storageLocation(), "sink.dummy.instance1", Sink::Storage::DataStore::ReadWrite);
            auto t = store.createTransaction();
            t.openDatabase("__metadata").write("databaseVersion", QByteArray::number(1));
            t.commit();
        }

        auto upgradeJob = Sink::Store::upgrade()
            .then([](const Sink::Store::UpgradeResult &result) {
                ASYNCVERIFY(result.upgradeExecuted);
                return KAsync::null();
            });
        VERIFYEXEC(upgradeJob);

        //FIXME
        // QTest::qWait(1000);
        // {
        //     Sink::Storage::DataStore::clearEnv();
        //     Sink::Storage::DataStore store(Sink::storageLocation(), "sink.dummy.instance1", Sink::Storage::DataStore::ReadOnly);
        //     auto version = Sink::Storage::DataStore::databaseVersion(store.createTransaction(Sink::Storage::DataStore::ReadOnly));
        //     QCOMPARE(version, Sink::latestDatabaseVersion());
        // }
    }

    void upgradeFromDbWithNoVersion()
    {
        Event event("sink.dummy.instance1");
        event.setProperty("uid", "testuid");
        event.setProperty("summary", "summaryValue");
        Sink::Store::create<Event>(event).exec().waitForFinished();

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        //force the db to an old version.
        Sink::Storage::DataStore store(Sink::storageLocation(), "sink.dummy.instance1", Sink::Storage::DataStore::ReadWrite);
        auto t = store.createTransaction();
        t.openDatabase("__metadata").remove("databaseVersion");
        t.commit();

        auto upgradeJob = Sink::Store::upgrade()
            .then([](const Sink::Store::UpgradeResult &result) {
                ASYNCVERIFY(result.upgradeExecuted);
                return KAsync::null();
            });
        VERIFYEXEC(upgradeJob);

        //FIXME
        // QTest::qWait(1000);
        // {
        //     Sink::Storage::DataStore::clearEnv();
        //     Sink::Storage::DataStore store(Sink::storageLocation(), "sink.dummy.instance1", Sink::Storage::DataStore::ReadOnly);
        //     auto version = Sink::Storage::DataStore::databaseVersion(store.createTransaction(Sink::Storage::DataStore::ReadOnly));
        //     QCOMPARE(version, Sink::latestDatabaseVersion());
        // }
    }
};

QTEST_MAIN(UpgradeTest)
#include "upgradetest.moc"
