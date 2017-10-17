#include <QtTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "dummyresource/domainadaptor.h"
#include "store.h"
#include "notifier.h"
#include "resourcecontrol.h"
#include "commands.h"
#include "entitybuffer.h"
#include "log.h"
#include "resourceconfig.h"
#include "notification_generated.h"
#include "test.h"
#include "testutils.h"
#include "adaptorfactoryregistry.h"

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
        VERIFYEXEC(Sink::Store::removeDataFromDisk("sink.dummy.instance1"));

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
        QUICK_TRY_VERIFY(gotNotification);
        HAWD::Dataset dataset("dummy_responsiveness", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();
        row.setValue("responsetime", duration);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);

        VERIFYEXEC(Sink::ResourceControl::shutdown("sink.dummy.instance1"));
    }

    void testWriteToFacade()
    {
        VERIFYEXEC(Sink::Store::removeDataFromDisk("sink.dummy.instance1"));

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
            query.resourceFilter("sink.dummy.instance1");
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));
        }
        auto allProcessedTime = time.elapsed();

        HAWD::Dataset dataset("dummy_write_to_facade", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();

        row.setValue("rows", num);
        row.setValue("append", (qreal)num / appendTime);
        row.setValue("total", (qreal)num / allProcessedTime);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
    }

    void testQueryByUid()
    {
        QTime time;
        time.start();
        // Measure query
        {
            time.start();
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");

            query.filter("uid", Sink::Query::Comparator("testuid"));
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Event>(query);
            QUICK_TRY_VERIFY(model->rowCount(QModelIndex()) == num);
        }
        auto queryTime = time.elapsed();

        HAWD::Dataset dataset("dummy_query_by_uid", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();
        row.setValue("rows", num);
        row.setValue("read", (qreal)num / queryTime);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
    }


    // This allows to run individual parts without doing a cleanup, but still cleaning up normally
    void testCleanupForCompleteTest()
    {
        VERIFYEXEC(Sink::Store::removeDataFromDisk("sink.dummy.instance1"));
    }

private:
    HAWD::State m_hawdState;
};

QTEST_MAIN(DummyResourceBenchmark)
#include "dummyresourcebenchmark.moc"
