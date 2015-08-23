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
#include "index.h"
#include <iostream>

#include "hawd/dataset.h"
#include "hawd/formatter.h"

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
        Akonadi2::Log::setDebugOutputLevel(Akonadi2::Log::Warning);
    }

    void initTestCase()
    {
        removeFromDisk("org.kde.test.instance1");
        removeFromDisk("org.kde.test.instance1.userqueue");
        removeFromDisk("org.kde.test.instance1.synchronizerqueue");
    }


    void testWriteInProcess()
    {
        int num = 10000;

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

        HAWD::Dataset dataset("generic_write_in_process", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();

        row.setValue("rows", num);
        row.setValue("append", appendTime);
        row.setValue("appendOps", (qreal)num/appendTime);
        row.setValue("total", allProcessedTime);
        row.setValue("totalOps", (qreal)num/allProcessedTime);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
    }

    void testWriteInProcessWithIndex()
    {
        int num = 50000;

        auto pipeline = QSharedPointer<Akonadi2::Pipeline>::create("org.kde.test.instance1");

        auto eventFactory = QSharedPointer<TestEventAdaptorFactory>::create();
        const QByteArray resourceIdentifier = "org.kde.test.instance1";
        auto eventIndexer = new Akonadi2::SimpleProcessor("eventIndexer", [eventFactory, resourceIdentifier](const Akonadi2::PipelineState &state, const Akonadi2::Entity &entity, Akonadi2::Storage::Transaction &transaction) {
            auto adaptor = eventFactory->createAdaptor(entity);
            Akonadi2::ApplicationDomain::Event event(resourceIdentifier, state.key(), -1, adaptor);
            Akonadi2::ApplicationDomain::TypeImplementation<Akonadi2::ApplicationDomain::Event>::index(event, transaction);

            //Create a bunch of indexes
            for (int i = 0; i < 10; i++) {
                Index ridIndex(QString("index.index%1").arg(i).toLatin1(), transaction);
                ridIndex.add("foo", event.identifier());
            }
        });

        pipeline->setPreprocessors("event", Akonadi2::Pipeline::NewPipeline, QVector<Akonadi2::Preprocessor*>() << eventIndexer);
        pipeline->setAdaptorFactory("event", eventFactory);

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

        HAWD::Dataset dataset("generic_write_in_process_with_indexes", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();

        row.setValue("rows", num);
        row.setValue("append", appendTime);
        row.setValue("appendOps", (qreal)num/appendTime);
        row.setValue("total", allProcessedTime);
        row.setValue("totalOps", (qreal)num/allProcessedTime);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
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

private:
    HAWD::State m_hawdState;
};

QTEST_MAIN(GenericResourceBenchmark)
#include "genericresourcebenchmark.moc"
