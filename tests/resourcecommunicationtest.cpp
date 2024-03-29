#include <QTest>
#include <QSignalSpy>

#include "resourceaccess.h"
#include "listener.h"
#include "commands.h"
#include "test.h"
#include "handshake_generated.h"

/**
 * Test that resourceaccess and listener work together.
 */
class ResourceCommunicationTest : public QObject
{
    Q_OBJECT
private slots:
    void testConnect()
    {
        const QByteArray resourceIdentifier("test");
        Listener listener(resourceIdentifier, "");
        Sink::ResourceAccess resourceAccess(resourceIdentifier, "");

        QSignalSpy spy(&resourceAccess, &Sink::ResourceAccess::ready);
        resourceAccess.open();
        QTRY_COMPARE(spy.size(), 1);
    }

    void testHandshake()
    {
        const QByteArray resourceIdentifier("test");
        Listener listener(resourceIdentifier, "");
        Sink::ResourceAccess resourceAccess(resourceIdentifier, "");
        resourceAccess.open();

        flatbuffers::FlatBufferBuilder fbb;
        auto name = fbb.CreateString("test");
        auto command = Sink::Commands::CreateHandshake(fbb, name);
        Sink::Commands::FinishHandshakeBuffer(fbb, command);
        VERIFYEXEC(resourceAccess.sendCommand(Sink::Commands::HandshakeCommand, fbb));
    }

    void testCommandLoop()
    {
        const QByteArray resourceIdentifier("test");
        Listener listener(resourceIdentifier, "");
        Sink::ResourceAccess resourceAccess(resourceIdentifier, "");
        resourceAccess.open();

        const int count = 500;
        int complete = 0;
        int errors = 0;
        for (int i = 0; i < count; i++) {
            auto result = resourceAccess.sendCommand(Sink::Commands::PingCommand)
                                .then([&resourceAccess, &errors, &complete](const KAsync::Error &error) {
                                    complete++;
                                    if (error) {
                                        qWarning() << error.errorMessage;
                                        errors++;
                                    }
                                })
                                .exec();
        }
        QTRY_COMPARE(complete, count);
        QVERIFY(!errors);
    }

    void testResourceAccessReuse()
    {
        const QByteArray resourceIdentifier("test");
        Listener listener(resourceIdentifier, "");
        Sink::ResourceAccess resourceAccess(resourceIdentifier, "");
        resourceAccess.open();

        const int count = 10;
        int complete = 0;
        int errors = 0;
        for (int i = 0; i < count; i++) {
            VERIFYEXEC(resourceAccess.sendCommand(Sink::Commands::PingCommand)
                .then([&resourceAccess, &errors, &complete](const KAsync::Error &error) {
                    complete++;
                    if (error) {
                        qWarning() << error.errorMessage;
                        errors++;
                    }
                    resourceAccess.close();
                    resourceAccess.open();
                }));
        }
        QTRY_COMPARE(complete, count);
        QVERIFY(!errors);
    }

    void testAccessFactory()
    {
        const QByteArray resourceIdentifier("test");
        Listener listener(resourceIdentifier, "");
        QWeakPointer<Sink::ResourceAccess> weakRef;
        QTime time;
        time.start();
        {
            auto resourceAccess = Sink::ResourceAccessFactory::instance().getAccess(resourceIdentifier, "");
            weakRef = resourceAccess.toWeakRef();
            resourceAccess->open();
            resourceAccess->sendCommand(Sink::Commands::PingCommand).then([resourceAccess]() { qDebug() << "Ping complete";  }).exec();
        }
        QVERIFY(weakRef.toStrongRef());
        QTRY_VERIFY(!weakRef.toStrongRef());
        qDebug() << "time.elapsed " << time.elapsed();
        QVERIFY(time.elapsed() < 3500);
        QVERIFY(time.elapsed() > 2500);
    }

    void testResourceAccessShutdown()
    {
        const QByteArray resourceIdentifier("test");
        Listener listener(resourceIdentifier, "");
        Sink::ResourceAccess resourceAccess(resourceIdentifier, "");
        resourceAccess.open();
        QTRY_VERIFY(resourceAccess.isReady());
        VERIFYEXEC(resourceAccess.shutdown());
        QTRY_VERIFY(!resourceAccess.isReady());
    }

    void testResourceAccessShutdownWithCommand()
    {
        const QByteArray resourceIdentifier("test");
        for (int i = 0; i < 10; i++) {
            Listener listener(resourceIdentifier, "");
            auto resourceAccess = Sink::ResourceAccessFactory::instance().getAccess(resourceIdentifier, "");
            //This automatically connects
            VERIFYEXEC(resourceAccess->sendCommand(Sink::Commands::PingCommand));
            QVERIFY(resourceAccess->isReady());
            VERIFYEXEC(resourceAccess->shutdown());
        }
    }

    /**
     * Make sure we handle a shutdown while commands being written to the resource.
     */
    void testResourceAccessShutdownWithCommand2()
    {
        const QByteArray resourceIdentifier("test");
        Listener listener(resourceIdentifier, "");
        auto resourceAccess = Sink::ResourceAccessFactory::instance().getAccess(resourceIdentifier, "");
        for (int i = 0; i < 10; i++) {
            resourceAccess->sendCommand(Sink::Commands::PingCommand).exec();
        }
        resourceAccess->shutdown().exec();
        for (int i = 0; i < 10; i++) {
            resourceAccess->sendCommand(Sink::Commands::PingCommand).exec();
        }
    }
};

QTEST_MAIN(ResourceCommunicationTest)
#include "resourcecommunicationtest.moc"
