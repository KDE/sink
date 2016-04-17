#include <QtTest>

#include "resourceaccess.h"
#include "listener.h"
#include "commands.h"
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
        auto result = resourceAccess.sendCommand(Sink::Commands::HandshakeCommand, fbb).exec();
        result.waitForFinished();
        QVERIFY(!result.errorCode());
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
                              .then<void>([&complete]() { complete++; },
                                  [&errors, &complete](int error, const QString &msg) {
                                      qWarning() << msg;
                                      errors++;
                                      complete++;
                                  })
                              .exec();
        }
        QTRY_COMPARE(complete, count);
        QVERIFY(!errors);
    }

    void testResourceAccessReuse()
    {
        qDebug();
        const QByteArray resourceIdentifier("test");
        Listener listener(resourceIdentifier, "");
        Sink::ResourceAccess resourceAccess(resourceIdentifier, "");
        resourceAccess.open();

        const int count = 10;
        int complete = 0;
        int errors = 0;
        for (int i = 0; i < count; i++) {
            resourceAccess.sendCommand(Sink::Commands::PingCommand)
                .then<void>([&complete]() { complete++; },
                    [&errors, &complete](int error, const QString &msg) {
                        qWarning() << msg;
                        errors++;
                        complete++;
                    })
                .then<void>([&resourceAccess]() {
                    resourceAccess.close();
                    resourceAccess.open();
                })
                .exec()
                .waitForFinished();
        }
        QTRY_COMPARE(complete, count);
        QVERIFY(!errors);
    }
};

QTEST_MAIN(ResourceCommunicationTest)
#include "resourcecommunicationtest.moc"
