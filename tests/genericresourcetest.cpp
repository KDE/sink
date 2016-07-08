#include <QtTest>

#include <QString>

#include "testimplementations.h"

#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"
#include "commands.h"
#include "entitybuffer.h"
#include "pipeline.h"
#include "genericresource.h"
#include "definitions.h"

/**
 * Test of the generic resource implementation.
 *
 * This test relies on a working pipeline implementation, and writes to storage.
 */
class GenericResourceTest : public QObject
{
    Q_OBJECT
private slots:

    void init()
    {
        Sink::GenericResource::removeFromDisk("org.kde.test.instance1");
    }

    /// Ensure the resource can process messages
    void testProcessCommand()
    {
        flatbuffers::FlatBufferBuilder eventFbb;
        eventFbb.Clear();
        {
            auto summary = eventFbb.CreateString("summary");
            Sink::ApplicationDomain::Buffer::EventBuilder eventBuilder(eventFbb);
            eventBuilder.add_summary(summary);
            auto eventLocation = eventBuilder.Finish();
            Sink::ApplicationDomain::Buffer::FinishEventBuffer(eventFbb, eventLocation);
        }

        flatbuffers::FlatBufferBuilder localFbb;
        {
            auto uid = localFbb.CreateString("testuid");
            auto localBuilder = Sink::ApplicationDomain::Buffer::EventBuilder(localFbb);
            localBuilder.add_uid(uid);
            auto location = localBuilder.Finish();
            Sink::ApplicationDomain::Buffer::FinishEventBuffer(localFbb, location);
        }

        flatbuffers::FlatBufferBuilder entityFbb;
        Sink::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, eventFbb.GetBufferPointer(), eventFbb.GetSize(), localFbb.GetBufferPointer(), localFbb.GetSize());

        flatbuffers::FlatBufferBuilder fbb;
        auto type = fbb.CreateString(Sink::ApplicationDomain::getTypeName<Sink::ApplicationDomain::Event>().toStdString().data());
        auto delta = fbb.CreateVector<uint8_t>(entityFbb.GetBufferPointer(), entityFbb.GetSize());
        Sink::Commands::CreateEntityBuilder builder(fbb);
        builder.add_domainType(type);
        builder.add_delta(delta);
        auto location = builder.Finish();
        Sink::Commands::FinishCreateEntityBuffer(fbb, location);

        const QByteArray command(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
        {
            flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command.data()), command.size());
            QVERIFY(Sink::Commands::VerifyCreateEntityBuffer(verifyer));
        }

        // Actual test
        auto pipeline = QSharedPointer<Sink::Pipeline>::create("org.kde.test.instance1");
        QSignalSpy revisionSpy(pipeline.data(), SIGNAL(revisionUpdated(qint64)));
        QVERIFY(revisionSpy.isValid());
        TestResource resource("org.kde.test.instance1", pipeline);
        resource.processCommand(Sink::Commands::CreateEntityCommand, command);
        resource.processCommand(Sink::Commands::CreateEntityCommand, command);
        resource.processAllMessages().exec().waitForFinished();
        QCOMPARE(revisionSpy.last().at(0).toInt(), 2);
    }
};

QTEST_MAIN(GenericResourceTest)
#include "genericresourcetest.moc"
