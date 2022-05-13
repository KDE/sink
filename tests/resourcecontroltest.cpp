#include <QTest>

#include "dummyresource/resourcefactory.h"
#include "resourcecontrol.h"
#include "store.h"
#include "test.h"
#include "resourceconfig.h"
#include "resourceaccess.h"
#include "commands.h"

/**
 * Test starting and stopping of resources.
 */
class ResourceControlTest : public QObject
{
    Q_OBJECT

    KAsync::Job<bool> socketIsAvailable(const QByteArray &identifier)
    {
        return Sink::ResourceAccess::connectToServer(identifier)
            .then<void, QSharedPointer<QLocalSocket>>(
                [&](const KAsync::Error &error, QSharedPointer<QLocalSocket> socket) {
                    if (error) {
                        return KAsync::value(false);
                    }
                    socket->close();
                    return KAsync::value(true);
                });

    }

    bool blockingSocketIsAvailable(const QByteArray &identifier)
    {
        auto job = socketIsAvailable(identifier);
        auto future = job.exec();
        future.waitForFinished();
        return future.value();
    }

private slots:

    void initTestCase()
    {
        Sink::Test::initTest();
        auto factory = Sink::ResourceFactory::load("sink.dummy");
        QVERIFY(factory);
        ::DummyResource::removeFromDisk("sink.dummy.instance1");
        ResourceConfig::addResource("sink.dummy.instance1", "sink.dummy");
        ::DummyResource::removeFromDisk("sink.dummy.instance2");
        ResourceConfig::addResource("sink.dummy.instance2", "sink.dummy");
    }

    void testResourceStart()
    {
        VERIFYEXEC(Sink::ResourceControl::start("sink.dummy.instance1"));
        QVERIFY(blockingSocketIsAvailable("sink.dummy.instance1"));
    }

    void testResourceShutdown()
    {
        QVERIFY(!blockingSocketIsAvailable("sink.dummy.instance2"));
        VERIFYEXEC(Sink::ResourceControl::start("sink.dummy.instance2"));
        QVERIFY(blockingSocketIsAvailable("sink.dummy.instance2"));
        VERIFYEXEC(Sink::ResourceControl::shutdown("sink.dummy.instance2"));
        QVERIFY(!blockingSocketIsAvailable("sink.dummy.instance2"));
    }

    //This will produce a race where the synchronize command starts the resource,
    //the shutdown command doesn't shutdown because it doesn't realize that the resource is up,
    //and the resource ends up getting started, but doing nothing.
    void testResourceShutdownAfterStartByCommand()
    {
        QVERIFY(!blockingSocketIsAvailable("sink.dummy.instance2"));
        auto future = Sink::Store::synchronize(Sink::SyncScope{}.resourceFilter("sink.dummy.instance2")).exec();

        VERIFYEXEC(Sink::ResourceControl::shutdown("sink.dummy.instance2"));

        QVERIFY(!blockingSocketIsAvailable("sink.dummy.instance2"));
    }

    /**
     * An existing live-query should not restart the resource due to revisionReplayedCommands.
     * This was introduced for tests, in regular use the resources are running during the whole query anyways,
     * because a live query will start the resource via an explicit call to open().
     */
    void testRevisionReplayedAfterShutdown()
    {
        //Prepare
        const QByteArray identifier{"sink.dummy.instance2"};
        QVERIFY(!blockingSocketIsAvailable(identifier));
        VERIFYEXEC(Sink::ResourceControl::start(identifier));
        QVERIFY(blockingSocketIsAvailable(identifier));
        auto resourceAccess = Sink::ResourceAccessFactory::instance().getAccess(identifier, ResourceConfig::getResourceType(identifier));

        //Shutdown and immediately send a revision replayed command
        VERIFYEXEC(Sink::ResourceControl::shutdown(identifier));
        VERIFYEXEC_FAIL(resourceAccess->sendRevisionReplayedCommand(1));

        //This should not start the resource again
        QVERIFY(!blockingSocketIsAvailable("sink.dummy.instance2"));
    }

    void testAbortCommandsOnShutdown()
    {
        const QByteArray identifier{"sink.dummy.instance1"};
        VERIFYEXEC(Sink::ResourceControl::shutdown(identifier));
        auto resourceAccess = Sink::ResourceAccessFactory::instance().getAccess(identifier, ResourceConfig::getResourceType(identifier));
        resourceAccess->shutdown().exec();
        //This operation should be aborted by the shutdown operation
        VERIFYEXEC_FAIL(Sink::ResourceControl::start(identifier));
    }

    void testResourceShutdownRestartLoop()
    {
        const QByteArray identifier{"sink.dummy.instance1"};
        VERIFYEXEC(Sink::ResourceControl::shutdown(identifier));
        QVERIFY(!blockingSocketIsAvailable(identifier));
        for (int i = 0; i < 10; i++) {
            Sink::ResourceControl::start(identifier).exec().waitForFinished();
            Sink::ResourceControl::shutdown(identifier).exec().waitForFinished();
        }
        QVERIFY(!blockingSocketIsAvailable(identifier));
    }

    /**
     * This test used to trigger a SIGPIPE, before we started to abort the socket on shutdown.
     */
    void testResourceShutdownRestartWithCommandLoop()
    {
        const QByteArray identifier{"sink.dummy.instance1"};
        VERIFYEXEC(Sink::ResourceControl::shutdown(identifier));
        QVERIFY(!blockingSocketIsAvailable(identifier));
        for (int i = 0; i < 10; i++) {
            auto resourceAccess = Sink::ResourceAccessFactory::instance().getAccess(identifier, ResourceConfig::getResourceType(identifier));
            resourceAccess->sendCommand(Sink::Commands::PingCommand).exec();
            resourceAccess->shutdown().exec().waitForFinished();
            Sink::ResourceControl::start(identifier).exec().waitForFinished();
        }

        VERIFYEXEC(Sink::ResourceControl::shutdown(identifier));
        QVERIFY(!blockingSocketIsAvailable(identifier));
    }

    /**
     * This seems to somehow corrupt the stack and crashes with
     *  malloc(): unaligned tcache chunk detected
     */
    void testResourceShutdownCrash()
    {
        QSKIP("Results in a crash");
        const QByteArray identifier{"sink.dummy.instance1"};
        VERIFYEXEC(Sink::ResourceControl::shutdown(identifier));
        QVERIFY(!blockingSocketIsAvailable(identifier));
        {
            auto resourceAccess = Sink::ResourceAccessFactory::instance().getAccess(identifier, ResourceConfig::getResourceType(identifier));
            QTest::qWait(500);
            resourceAccess->shutdown().exec().waitForFinished();
        }
        Sink::ResourceControl::start(identifier).exec().waitForFinished();

        VERIFYEXEC(Sink::ResourceControl::shutdown(identifier));
        QVERIFY(!blockingSocketIsAvailable(identifier));
    }

};

QTEST_MAIN(ResourceControlTest)
#include "resourcecontroltest.moc"
