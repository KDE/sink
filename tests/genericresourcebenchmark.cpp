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
#include "definitions.h"
#include "domainadaptor.h"
#include <iostream>

class TestResource : public Akonadi2::GenericResource
{
public:
    TestResource(const QByteArray &instanceIdentifier, QSharedPointer<Akonadi2::Pipeline> pipeline)
        : Akonadi2::GenericResource(instanceIdentifier, pipeline)
    {
    }

    KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE
    {
        return KAsync::null<void>();
    }
};

class TestEventAdaptorFactory : public DomainTypeAdaptorFactory<Akonadi2::ApplicationDomain::Event, Akonadi2::ApplicationDomain::Buffer::Event, Akonadi2::ApplicationDomain::Buffer::EventBuilder>
{
public:
    TestEventAdaptorFactory()
        : DomainTypeAdaptorFactory()
    {
    }

    virtual ~TestEventAdaptorFactory() {};
};


static void removeFromDisk(const QString &name)
{
    Akonadi2::Storage store(Akonadi2::storageLocation(), name, Akonadi2::Storage::ReadWrite);
    store.removeFromDisk();
}

static QByteArray createEntityBuffer()
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

    return QByteArray(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
}

class GenericResourceBenchmark : public QObject
{
    Q_OBJECT
private Q_SLOTS:

    void init()
    {
        removeFromDisk("org.kde.test.instance1");
        removeFromDisk("org.kde.test.instance1.userqueue");
        removeFromDisk("org.kde.test.instance1.synchronizerqueue");
        Akonadi2::Log::setDebugOutputLevel(Akonadi2::Log::Warning);
        qDebug();
        qDebug() << "-----------------------------------------";
        qDebug();
    }


    void testWriteInProcess()
    {
        int num = 50000;

        auto pipeline = QSharedPointer<Akonadi2::Pipeline>::create("org.kde.test.instance1");
        TestResource resource("org.kde.test.instance1", pipeline);

        auto command = createEntityBuffer();

        QTime time;
        time.start();

        for (int i = 0; i < num; i++) {
            resource.processCommand(Akonadi2::Commands::CreateEntityCommand, command);
        }
        auto appendTime = time.elapsed();

        //Wait until all messages have been processed
        resource.processAllMessages().exec().waitForFinished();

        auto allProcessedTime = time.elapsed();

        std::cout << "Append to messagequeue " << appendTime << " /sec " << num*1000/appendTime << std::endl;
        std::cout << "All processed: " << allProcessedTime << " /sec " << num*1000/allProcessedTime << std::endl;
    }

    void testCreateCommand()
    {
        Akonadi2::ApplicationDomain::Event event;

        QBENCHMARK {
            auto mFactory = new TestEventAdaptorFactory;
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

QTEST_MAIN(GenericResourceBenchmark)
#include "genericresourcebenchmark.moc"
