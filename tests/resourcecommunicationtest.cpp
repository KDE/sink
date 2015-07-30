#include <QtTest>

#include "resourceaccess.h"
#include "listener.h"
#include "commands.h"
#include "handshake_generated.h"

class ResourceCommunicationTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testConnect()
    {
        const QByteArray resourceIdentifier("test");
        Listener listener(resourceIdentifier);
        Akonadi2::ResourceAccess resourceAccess(resourceIdentifier);

        QSignalSpy spy(&resourceAccess, &Akonadi2::ResourceAccess::ready);
        resourceAccess.open();
        QTRY_COMPARE(spy.size(), 1);
    }

    void testHandshake()
    {
        const QByteArray resourceIdentifier("test");
        Listener listener(resourceIdentifier);
        Akonadi2::ResourceAccess resourceAccess(resourceIdentifier);
        resourceAccess.open();

        flatbuffers::FlatBufferBuilder fbb;
        auto name = fbb.CreateString("test");
        auto command = Akonadi2::CreateHandshake(fbb, name);
        Akonadi2::FinishHandshakeBuffer(fbb, command);
        auto result = resourceAccess.sendCommand(Akonadi2::Commands::HandshakeCommand, fbb).exec();
        result.waitForFinished();
        QVERIFY(!result.errorCode());
    }
};

QTEST_MAIN(ResourceCommunicationTest)
#include "resourcecommunicationtest.moc"
