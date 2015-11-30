#include <QtTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "dummyresource/domainadaptor.h"
#include "clientapi.h"
#include "commands.h"
#include "entitybuffer.h"
#include "pipeline.h"
#include "log.h"
#include "resourceconfig.h"

#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"

#include <iostream>

/**
 * Benchmark full system with the dummy resource implementation.
 */
class DummyResourceBenchmark : public QObject
{
    Q_OBJECT
private:
    int num;
private Q_SLOTS:
    void initTestCase()
    {
        Akonadi2::Log::setDebugOutputLevel(Akonadi2::Log::Warning);
        auto factory = Akonadi2::ResourceFactory::load("org.kde.dummy");
        QVERIFY(factory);
        ResourceConfig::addResource("org.kde.dummy.instance1", "org.kde.dummy");
        num = 5000;
    }

    void cleanup()
    {
    }

    static KAsync::Job<void> waitForCompletion(QList<KAsync::Future<void> > &futures)
    {
        auto context = new QObject;
        return KAsync::start<void>([futures, context](KAsync::Future<void> &future) {
            const auto total = futures.size();
            auto count = QSharedPointer<int>::create();
            int i = 0;
            for (KAsync::Future<void> subFuture : futures) {
                i++;
                if (subFuture.isFinished()) {
                    *count += 1;
                    continue;
                }
                //FIXME bind lifetime all watcher to future (repectively the main job
                auto watcher = QSharedPointer<KAsync::FutureWatcher<void> >::create();
                QObject::connect(watcher.data(), &KAsync::FutureWatcher<void>::futureReady,
                [count, total, &future](){
                    *count += 1;
                    if (*count == total) {
                        future.setFinished();
                    }
                });
                watcher->setFuture(subFuture);
                context->setProperty(QString("future%1").arg(i).toLatin1().data(), QVariant::fromValue(watcher));
            }
        }).then<void>([context]() {
            delete context;
        });
    }

    void testWriteToFacade()
    {
        DummyResource::removeFromDisk("org.kde.dummy.instance1");

        QTime time;
        time.start();
        QList<KAsync::Future<void> > waitCondition;
        for (int i = 0; i < num; i++) {
            Akonadi2::ApplicationDomain::Event event("org.kde.dummy.instance1");
            event.setProperty("uid", "testuid");
            QCOMPARE(event.getProperty("uid").toByteArray(), QByteArray("testuid"));
            event.setProperty("summary", "summaryValue");
            waitCondition << Akonadi2::Store::create<Akonadi2::ApplicationDomain::Event>(event).exec();
        }
        waitForCompletion(waitCondition).exec().waitForFinished();
        auto appendTime = time.elapsed();

        //Ensure everything is processed
        {
            Akonadi2::Query query;
            query.resources << "org.kde.dummy.instance1";
            query.syncOnDemand = false;
            query.processAll = true;
            Akonadi2::Store::synchronize(query).exec().waitForFinished();
        }
        auto allProcessedTime = time.elapsed();
        qDebug() << "Append to messagequeue " << appendTime;
        qDebug() << "All processed: " << allProcessedTime << "/sec " << num*1000/allProcessedTime;
    }

    void testQueryByUid()
    {
        QTime time;
        time.start();
        //Measure query
        {
            time.start();
            Akonadi2::Query query;
            query.resources << "org.kde.dummy.instance1";
            query.syncOnDemand = false;
            query.processAll = false;

            query.propertyFilter.insert("uid", "testuid");
            auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Event>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), num);
        }
        qDebug() << "Query Time: " << time.elapsed() << "/sec " << num*1000/time.elapsed();
    }

    void testWriteInProcess()
    {
        DummyResource::removeFromDisk("org.kde.dummy.instance1");
        QTime time;
        time.start();

        auto pipeline = QSharedPointer<Akonadi2::Pipeline>::create("org.kde.dummy.instance1");
        DummyResource resource("org.kde.dummy.instance1", pipeline);

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

    //This allows to run individual parts without doing a cleanup, but still cleaning up normally
    void testCleanupForCompleteTest()
    {
        DummyResource::removeFromDisk("org.kde.dummy.instance1");
    }
};

QTEST_MAIN(DummyResourceBenchmark)
#include "dummyresourcebenchmark.moc"
