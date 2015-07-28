#include <QtTest>

#include <QString>

#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"
#include "commands.h"
#include "entitybuffer.h"
#include "pipeline.h"
#include "genericresource.h"

class TestResource : public Akonadi2::GenericResource
{
public:
    TestResource(const QByteArray &instanceIdentifier)
        : Akonadi2::GenericResource(instanceIdentifier)
    {
    }

    KAsync::Job<void> synchronizeWithSource(Akonadi2::Pipeline *pipeline) Q_DECL_OVERRIDE
    {
        return KAsync::null<void>();
    }

    void configurePipeline(Akonadi2::Pipeline *pipeline) Q_DECL_OVERRIDE
    {
        GenericResource::configurePipeline(pipeline);
    }
};


static void removeFromDisk(const QString &name)
{
    Akonadi2::Storage store(Akonadi2::Store::storageLocation(), name, Akonadi2::Storage::ReadWrite);
    store.removeFromDisk();
}

class GenericResourceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:

    void init()
    {
        qDebug();
        qDebug() << "-----------------------------------------";
        qDebug();
    }

    void testProcessCommand()
    {
        flatbuffers::FlatBufferBuilder eventFbb;
        eventFbb.Clear();
        {
            auto summary = eventFbb.CreateString("summary");
            Akonadi2::ApplicationDomain::Buffer::EventBuilder eventBuilder(eventFbb);
            eventBuilder.add_summary(summary);
            auto eventLocation = eventBuilder.Finish();
            Akonadi2::ApplicationDomain::Buffer::FinishEventBuffer(eventFbb, eventLocation);
        }

        flatbuffers::FlatBufferBuilder localFbb;
        {
            auto uid = localFbb.CreateString("testuid");
            auto localBuilder = Akonadi2::ApplicationDomain::Buffer::EventBuilder(localFbb);
            localBuilder.add_uid(uid);
            auto location = localBuilder.Finish();
            Akonadi2::ApplicationDomain::Buffer::FinishEventBuffer(localFbb, location);
        }

        flatbuffers::FlatBufferBuilder entityFbb;
        Akonadi2::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, eventFbb.GetBufferPointer(), eventFbb.GetSize(), localFbb.GetBufferPointer(), localFbb.GetSize());

        flatbuffers::FlatBufferBuilder fbb;
        auto type = fbb.CreateString(Akonadi2::ApplicationDomain::getTypeName<Akonadi2::ApplicationDomain::Event>().toStdString().data());
        auto delta = fbb.CreateVector<uint8_t>(entityFbb.GetBufferPointer(), entityFbb.GetSize());
        Akonadi2::Commands::CreateEntityBuilder builder(fbb);
        builder.add_domainType(type);
        builder.add_delta(delta);
        auto location = builder.Finish();
        Akonadi2::Commands::FinishCreateEntityBuffer(fbb, location);

        const QByteArray command(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
        {
            flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command.data()), command.size());
            QVERIFY(Akonadi2::Commands::VerifyCreateEntityBuffer(verifyer));
        }

        //Actual test
        Akonadi2::Pipeline pipeline("org.kde.test.instance1");
        QSignalSpy revisionSpy(&pipeline, SIGNAL(revisionUpdated(qint64)));
        TestResource resource("org.kde.test.instance1");
        resource.configurePipeline(&pipeline);
        resource.processCommand(Akonadi2::Commands::CreateEntityCommand, command, &pipeline);
        resource.processCommand(Akonadi2::Commands::CreateEntityCommand, command, &pipeline);

        QVERIFY(revisionSpy.isValid());
        QTRY_COMPARE(revisionSpy.count(), 2);
        QTest::qWait(100);
        QCOMPARE(revisionSpy.count(), 2);
    }
};

QTEST_MAIN(GenericResourceTest)
#include "genericresourcetest.moc"
