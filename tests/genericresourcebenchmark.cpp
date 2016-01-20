#include <QtTest>

#include <QString>

#include "testimplementations.h"

#include "event_generated.h"
#include "createentity_generated.h"
#include "commands.h"
#include "entitybuffer.h"
#include "pipeline.h"
#include "genericresource.h"
#include "definitions.h"
#include "domainadaptor.h"
#include "index.h"

#include "hawd/dataset.h"
#include "hawd/formatter.h"


static void removeFromDisk(const QString &name)
{
    Sink::Storage store(Sink::storageLocation(), name, Sink::Storage::ReadWrite);
    store.removeFromDisk();
}

static QByteArray createEntityBuffer()
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

    return QByteArray(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
}

class IndexUpdater : public Sink::Preprocessor {
public:
    void newEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &newEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        for (int i = 0; i < 10; i++) {
            Index ridIndex(QString("index.index%1").arg(i).toLatin1(), transaction);
            ridIndex.add("foo", uid);
        }
    }

    void modifiedEntity(const QByteArray &key, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, const Sink::ApplicationDomain::BufferAdaptor &newEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
    }

    void deletedEntity(const QByteArray &key, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
    }
};

/**
 * Benchmark write performance of generic resource implementation including queues and pipeline.
 */
class GenericResourceBenchmark : public QObject
{
    Q_OBJECT
private Q_SLOTS:

    void init()
    {
        Sink::Log::setDebugOutputLevel(Sink::Log::Warning);
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

        auto pipeline = QSharedPointer<Sink::Pipeline>::create("org.kde.test.instance1");
        TestResource resource("org.kde.test.instance1", pipeline);

        auto command = createEntityBuffer();

        QTime time;
        time.start();

        for (int i = 0; i < num; i++) {
            resource.processCommand(Sink::Commands::CreateEntityCommand, command);
        }
        auto appendTime = time.elapsed();

        //Wait until all messages have been processed
        resource.processAllMessages().exec().waitForFinished();

        auto allProcessedTime = time.elapsed();

        // Print memory layout, RSS is what is in memory
        // std::system("exec pmap -x \"$PPID\"");

        HAWD::Dataset dataset("generic_write_in_process", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();

        row.setValue("rows", num);
        row.setValue("append", (qreal)num/appendTime);
        row.setValue("total", (qreal)num/allProcessedTime);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
    }

    void testWriteInProcessWithIndex()
    {
        int num = 50000;

        auto pipeline = QSharedPointer<Sink::Pipeline>::create("org.kde.test.instance1");

        auto eventFactory = QSharedPointer<TestEventAdaptorFactory>::create();
        const QByteArray resourceIdentifier = "org.kde.test.instance1";
        auto indexer = QSharedPointer<IndexUpdater>::create();

        pipeline->setPreprocessors("event", QVector<Sink::Preprocessor*>() << indexer.data());
        pipeline->setAdaptorFactory("event", eventFactory);

        TestResource resource("org.kde.test.instance1", pipeline);

        auto command = createEntityBuffer();

        QTime time;
        time.start();

        for (int i = 0; i < num; i++) {
            resource.processCommand(Sink::Commands::CreateEntityCommand, command);
        }
        auto appendTime = time.elapsed();

        //Wait until all messages have been processed
        resource.processAllMessages().exec().waitForFinished();

        auto allProcessedTime = time.elapsed();

        // Print memory layout, RSS is what is in memory
        // std::system("exec pmap -x \"$PPID\"");

        HAWD::Dataset dataset("generic_write_in_process_with_indexes", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();

        row.setValue("rows", num);
        row.setValue("append", (qreal)num/appendTime);
        row.setValue("total", (qreal)num/allProcessedTime);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
    }

    void testCreateCommand()
    {
        Sink::ApplicationDomain::Event event;

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
            auto delta = Sink::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
            auto location = Sink::Commands::CreateCreateEntity(fbb, type, delta);
            Sink::Commands::FinishCreateEntityBuffer(fbb, location);
        }
    }

private:
    HAWD::State m_hawdState;
};

QTEST_MAIN(GenericResourceBenchmark)
#include "genericresourcebenchmark.moc"
