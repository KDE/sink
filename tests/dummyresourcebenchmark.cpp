#include <QtTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "dummyresource/domainadaptor.h"
#include "clientapi.h"
#include "commands.h"
#include "entitybuffer.h"

#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"
#include <iostream>

static void removeFromDisk(const QString &name)
{
    Akonadi2::Storage store(Akonadi2::Store::storageLocation(), name, Akonadi2::Storage::ReadWrite);
    store.removeFromDisk();
}

class DummyResourceBenchmark : public QObject
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

    void testWriteToFacadeAndQueryByUid()
    {
        QTime time;
        time.start();
        int num = 10000;
        for (int i = 0; i < num; i++) {
            Akonadi2::ApplicationDomain::Event event;
            event.setProperty("uid", "testuid");
            QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
            event.setProperty("summary", "summaryValue");
            Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event, "org.kde.dummy");
        }
        auto appendTime = time.elapsed();

        //Ensure everything is processed
        {
            Akonadi2::Query query;
            query.resources << "org.kde.dummy";
            query.syncOnDemand = false;
            query.processAll = true;

            query.propertyFilter.insert("uid", "nonexistantuid");
            async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
            result.exec();
        }
        auto allProcessedTime = time.elapsed();

        //Measure query
        {
            time.start();
            Akonadi2::Query query;
            query.resources << "org.kde.dummy";
            query.syncOnDemand = false;
            query.processAll = false;

            query.propertyFilter.insert("uid", "testuid");
            async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(Akonadi2::Store::load<Akonadi2::ApplicationDomain::Event>(query));
            result.exec();
            QCOMPARE(result.size(), num);
        }
        qDebug() << "Append to messagequeue " << appendTime;
        qDebug() << "All processed: " << allProcessedTime << "/sec " << num*1000/allProcessedTime;
        qDebug() << "Query Time: " << time.elapsed() << "/sec " << num*1000/time.elapsed();
    }

    void testWriteInProcess()
    {
        QTime time;
        time.start();
        int num = 10000;

        Akonadi2::Pipeline pipeline("org.kde.dummy");
        QSignalSpy revisionSpy(&pipeline, SIGNAL(revisionUpdated()));
        DummyResource resource;
        resource.configurePipeline(&pipeline);

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

        for (int i = 0; i < num; i++) {
            resource.processCommand(Akonadi2::Commands::CreateEntityCommand, command, command.size(), &pipeline);
        }
        auto appendTime = time.elapsed();

        //Wait until all messages have been processed
        resource.processAllMessages().exec().waitForFinished();

        auto allProcessedTime = time.elapsed();

        std::cout << "Append to messagequeue " << appendTime << std::endl;
        std::cout << "All processed: " << allProcessedTime << "/sec " << num*1000/allProcessedTime << std::endl;
    }

    void testCreateCommand()
    {
        Akonadi2::ApplicationDomain::Event event;

        QBENCHMARK {
            auto mFactory = new DummyEventAdaptorFactory;
            static flatbuffers::FlatBufferBuilder entityFbb;
            entityFbb.Clear();
            mFactory->createBuffer(event, entityFbb);

            static flatbuffers::FlatBufferBuilder fbb;
            fbb.Clear();
            //This is the resource buffer type and not the domain type
            auto type = fbb.CreateString("event");
            // auto delta = fbb.CreateVector<uint8_t>(entityFbb.GetBufferPointer(), entityFbb.GetSize());
            auto delta = Akonadi2::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
            auto location = Akonadi2::Commands::CreateCreateEntity(fbb, type, delta);
            Akonadi2::Commands::FinishCreateEntityBuffer(fbb, location);
        }
    }
};

QTEST_MAIN(DummyResourceBenchmark)
#include "dummyresourcebenchmark.moc"
