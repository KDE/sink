#include <QtTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "dummyresource/domainadaptor.h"
#include "store.h"
#include "notifier.h"
#include "resourcecontrol.h"
#include "commands.h"
#include "entitybuffer.h"
#include "pipeline.h"
#include "log.h"
#include "resourceconfig.h"
#include "notification_generated.h"

#include "hawd/dataset.h"
#include "hawd/formatter.h"

#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"

/**
 * Benchmark full system with the dummy resource implementation.
 */
class DummyResourceBenchmark : public QObject
{
    Q_OBJECT
private:
    int num;
private slots:
    void initTestCase()
    {
        Sink::Log::setDebugOutputLevel(Sink::Log::Warning);
        auto factory = Sink::ResourceFactory::load("sink.dummy");
        QVERIFY(factory);
        ResourceConfig::addResource("sink.dummy.instance1", "sink.dummy");
        num = 5000;
    }

    void cleanup()
    {
    }

    // Ensure we can process a command in less than 0.1s
    void testCommandResponsiveness()
    {
        // Test responsiveness including starting the process.
        Sink::Store::removeDataFromDisk("sink.dummy.instance1").exec().waitForFinished();

        QTime time;
        time.start();

        Sink::ApplicationDomain::Event event("sink.dummy.instance1");
        event.setProperty("uid", "testuid");
        QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
        event.setProperty("summary", "summaryValue");

        auto notifier = QSharedPointer<Sink::Notifier>::create("sink.dummy.instance1", "sink.dummy");
        bool gotNotification = false;
        int duration = 0;
        notifier->registerHandler([&gotNotification, &duration, &time](const Sink::Notification &notification) {
            if (notification.type == Sink::Notification::RevisionUpdate) {
                gotNotification = true;
                duration = time.elapsed();
            }
        });

        Sink::Store::create<Sink::ApplicationDomain::Event>(event).exec();

        // Wait for notification
        QTRY_VERIFY(gotNotification);

        QVERIFY2(duration < 100, QString::fromLatin1("Processing a create command took more than 100ms: %1").arg(duration).toLatin1());
        Sink::ResourceControl::shutdown("sink.dummy.instance1").exec().waitForFinished();
        qDebug() << "Single command took [ms]: " << duration;
    }

    void testWriteToFacade()
    {
        Sink::Store::removeDataFromDisk("sink.dummy.instance1").exec().waitForFinished();

        QTime time;
        time.start();
        QList<KAsync::Future<void>> waitCondition;
        for (int i = 0; i < num; i++) {
            Sink::ApplicationDomain::Event event("sink.dummy.instance1");
            event.setProperty("uid", "testuid");
            QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
            event.setProperty("summary", "summaryValue");
            waitCondition << Sink::Store::create<Sink::ApplicationDomain::Event>(event).exec();
        }
        KAsync::waitForCompletion(waitCondition).exec().waitForFinished();
        auto appendTime = time.elapsed();

        // Ensure everything is processed
        {
            Sink::Query query;
            query.resources << "sink.dummy.instance1";
            Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();
        }
        auto allProcessedTime = time.elapsed();

        HAWD::Dataset dataset("dummy_write_to_facade", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();

        row.setValue("rows", num);
        row.setValue("append", (qreal)num / appendTime);
        row.setValue("total", (qreal)num / allProcessedTime);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);

        auto diskUsage = DummyResource::diskUsage("sink.dummy.instance1");
        qDebug() << "Database size [kb]: " << diskUsage / 1024;

        // Print memory layout, RSS is what is in memory
        // std::system("exec pmap -x \"$PPID\"");
    }

    void testQueryByUid()
    {
        QTime time;
        time.start();
        // Measure query
        {
            time.start();
            Sink::Query query;
            query.resources << "sink.dummy.instance1";

            query.propertyFilter.insert("uid", Sink::Query::Comparator("testuid"));
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), num);
        }
        auto queryTime = time.elapsed();

        HAWD::Dataset dataset("dummy_query_by_uid", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();
        row.setValue("rows", num);
        row.setValue("read", (qreal)num / queryTime);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
    }

    void testWriteInProcess()
    {
        Sink::Store::removeDataFromDisk("sink.dummy.instance1").exec().waitForFinished();
        QTime time;
        time.start();

        auto pipeline = QSharedPointer<Sink::Pipeline>::create("sink.dummy.instance1");
        DummyResource resource("sink.dummy.instance1", pipeline);

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

        for (int i = 0; i < num; i++) {
            resource.processCommand(Sink::Commands::CreateEntityCommand, command);
        }
        auto appendTime = time.elapsed();

        // Wait until all messages have been processed
        resource.processAllMessages().exec().waitForFinished();

        auto allProcessedTime = time.elapsed();

        HAWD::Dataset dataset("dummy_write_in_process", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();

        row.setValue("rows", num);
        row.setValue("append", (qreal)num / appendTime);
        row.setValue("total", (qreal)num / allProcessedTime);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);

        // Print memory layout, RSS is what is in memory
        // std::system("exec pmap -x \"$PPID\"");
    }

    void testCreateCommand()
    {
        Sink::ApplicationDomain::Event event;

        QBENCHMARK {
            auto mFactory = new DummyEventAdaptorFactory;
            static flatbuffers::FlatBufferBuilder entityFbb;
            entityFbb.Clear();
            mFactory->createBuffer(event, entityFbb);

            static flatbuffers::FlatBufferBuilder fbb;
            fbb.Clear();
            // This is the resource buffer type and not the domain type
            auto entityId = fbb.CreateString("");
            auto type = fbb.CreateString("event");
            // auto delta = fbb.CreateVector<uint8_t>(entityFbb.GetBufferPointer(), entityFbb.GetSize());
            auto delta = Sink::EntityBuffer::appendAsVector(fbb, entityFbb.GetBufferPointer(), entityFbb.GetSize());
            auto location = Sink::Commands::CreateCreateEntity(fbb, entityId, type, delta);
            Sink::Commands::FinishCreateEntityBuffer(fbb, location);
        }
    }

    // This allows to run individual parts without doing a cleanup, but still cleaning up normally
    void testCleanupForCompleteTest()
    {
        Sink::Store::removeDataFromDisk("sink.dummy.instance1").exec().waitForFinished();
    }

private:
    HAWD::State m_hawdState;
};

QTEST_MAIN(DummyResourceBenchmark)
#include "dummyresourcebenchmark.moc"
