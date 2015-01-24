#include <QtTest>

#include <QString>

// #include "dummycalendar_generated.h"
#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"
#include "dummyresource/resourcefactory.h"
#include "clientapi.h"
#include "commands.h"
#include "entitybuffer.h"

static void removeFromDisk(const QString &name)
{
    Akonadi2::Storage store(Akonadi2::Store::storageLocation(), name, Akonadi2::Storage::ReadWrite);
    store.removeFromDisk();
}

class DummyResourceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        auto factory = Akonadi2::ResourceFactory::load("org.kde.dummy");
        QVERIFY(factory);
        removeFromDisk("org.kde.dummy");
        removeFromDisk("org.kde.dummy.userqueue");
        removeFromDisk("org.kde.dummy.synchronizerqueue");
        removeFromDisk("org.kde.dummy.index.uid");
    }

    void cleanup()
    {
        removeFromDisk("org.kde.dummy");
        removeFromDisk("org.kde.dummy.userqueue");
        removeFromDisk("org.kde.dummy.synchronizerqueue");
        removeFromDisk("org.kde.dummy.index.uid");
    }

    void testProcessCommand()
    {
        flatbuffers::FlatBufferBuilder eventFbb;
        eventFbb.Clear();
        {
            auto summary = eventFbb.CreateString("summary");
            Akonadi2::Domain::Buffer::EventBuilder eventBuilder(eventFbb);
            eventBuilder.add_summary(summary);
            auto eventLocation = eventBuilder.Finish();
            Akonadi2::Domain::Buffer::FinishEventBuffer(eventFbb, eventLocation);
        }

        flatbuffers::FlatBufferBuilder localFbb;
        {
            auto uid = localFbb.CreateString("testuid");
            auto localBuilder = Akonadi2::Domain::Buffer::EventBuilder(localFbb);
            localBuilder.add_uid(uid);
            auto location = localBuilder.Finish();
            Akonadi2::Domain::Buffer::FinishEventBuffer(localFbb, location);
        }

        flatbuffers::FlatBufferBuilder entityFbb;
        Akonadi2::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, eventFbb.GetBufferPointer(), eventFbb.GetSize(), localFbb.GetBufferPointer(), localFbb.GetSize());

        flatbuffers::FlatBufferBuilder fbb;
        auto type = fbb.CreateString(Akonadi2::Domain::getTypeName<Akonadi2::Domain::Event>().toStdString().data());
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
        Akonadi2::Pipeline pipeline("org.kde.dummy");
        QSignalSpy revisionSpy(&pipeline, SIGNAL(revisionUpdated()));
        DummyResource resource;
        resource.configurePipeline(&pipeline);
        resource.processCommand(Akonadi2::Commands::CreateEntityCommand, command, command.size(), &pipeline);
        resource.processCommand(Akonadi2::Commands::CreateEntityCommand, command, command.size(), &pipeline);

        QVERIFY(revisionSpy.isValid());
        QTRY_COMPARE(revisionSpy.count(), 2);
        QTest::qWait(100);
        QCOMPARE(revisionSpy.count(), 2);
    }

    void testProperty()
    {
        Akonadi2::Domain::Event event;
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testWriteToFacadeAndQueryByUid()
    {
        Akonadi2::Domain::Event event;
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");
        Akonadi2::Store::create<Akonadi2::Domain::Event>(event, "org.kde.dummy");

        //TODO required to ensure all messages have been processed. The query should ensure this.
        QTest::qWait(300);

        Akonadi2::Query query;
        query.resources << "org.kde.dummy";
        query.syncOnDemand = false;

        query.propertyFilter.insert("uid", "testuid");
        async::SyncListResult<Akonadi2::Domain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::Domain::Event>(query));
        result.exec();
        QCOMPARE(result.size(), 1);
        auto value = result.first();
        QCOMPARE(value->getProperty("uid").toByteArray(), QByteArray("testuid"));
    }

    void testResourceSync()
    {
        Akonadi2::Pipeline pipeline("org.kde.dummy");
        DummyResource resource;
        resource.configurePipeline(&pipeline);
        auto job = resource.synchronizeWithSource(&pipeline);
        //TODO pass in optional timeout?
        auto future = job.exec();
        future.waitForFinished();
        QVERIFY(!future.errorCode());
        QTRY_VERIFY(future.isFinished());
        QVERIFY(!resource.error());
    }

    void testSyncAndFacade()
    {
        Akonadi2::Query query;
        query.resources << "org.kde.dummy";

        async::SyncListResult<Akonadi2::Domain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::Domain::Event>(query));
        result.exec();
        QVERIFY(!result.isEmpty());
        auto value = result.first();
        QVERIFY(!value->getProperty("summary").toString().isEmpty());
        qDebug() << value->getProperty("summary").toString();
    }

};

QTEST_MAIN(DummyResourceTest)
#include "dummyresourcetest.moc"
