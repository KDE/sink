#include <QtTest>

#include <QString>

#include <iostream>

#include "dummyresource/resourcefactory.h"
#include "dummyresource/domainadaptor.h"
#include "store.h"
#include "commands.h"
#include "entitybuffer.h"
#include "log.h"
#include "resourceconfig.h"
#include "definitions.h"
#include "facadefactory.h"
#include "adaptorfactoryregistry.h"

#include "hawd/dataset.h"
#include "hawd/formatter.h"

#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"

#include "getrssusage.h"

static double variance(const QList<double> &values)
{
    double mean = 0;
    for (auto value : values) {
        mean += value;
    }
    mean = mean / static_cast<double>(values.size());
    double variance = 0;
    for (auto value : values) {
        variance += pow(static_cast<double>(value) - mean, 2);
    }
    variance = variance / static_cast<double>(values.size() - 1);
    return variance;
}

static double maxDifference(const QList<double> &values)
{
    auto max = values.first();
    auto min = values.first();
    for (auto value : values) {
        if (value > max) {
            max = value;
        }
        if (value < min) {
            min = value;
        }
    }
    return max - min;
}

static QByteArray createEntityBuffer(int &bufferSize)
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
    bufferSize = entityFbb.GetSize();

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

/**
 * Benchmark writing in the synchronizer process.
 */
class DummyResourceWriteBenchmark : public QObject
{
    Q_OBJECT

    QList<double> mRssGrowthPerEntity;
    QList<double> mTimePerEntity;

    void writeInProcess(int num)
    {
        DummyResource::removeFromDisk("sink.dummy.instance1");


        QTime time;
        time.start();

        auto factory = new ::DummyResourceFactory;
        factory->registerFacades("dummy", Sink::FacadeFactory::instance());
        factory->registerAdaptorFactories("dummy", Sink::AdaptorFactoryRegistry::instance());

        ::DummyResource resource(Sink::ResourceContext{"sink.dummy.instance1", "dummy", Sink::AdaptorFactoryRegistry::instance().getFactories("dummy")});

        int bufferSize = 0;
        auto command = createEntityBuffer(bufferSize);

        const auto startingRss = getCurrentRSS();
        for (int i = 0; i < num; i++) {
            resource.processCommand(Sink::Commands::CreateEntityCommand, command);
        }
        auto appendTime = time.elapsed();
        Q_UNUSED(appendTime);
        auto bufferSizeTotal = bufferSize * num;

        // Wait until all messages have been processed
        resource.processAllMessages().exec().waitForFinished();

        auto allProcessedTime = time.elapsed();

        const auto finalRss = getCurrentRSS();
        const auto rssGrowth = finalRss - startingRss;
        // Since the database is memory mapped it is attributted to the resident set size.
        const auto rssWithoutDb = finalRss - DummyResource::diskUsage("sink.dummy.instance1");
        const auto peakRss = getPeakRSS();
        // How much peak deviates from final rss in percent
        const auto percentageRssError = static_cast<double>(peakRss - finalRss) * 100.0 / static_cast<double>(finalRss);
        auto rssGrowthPerEntity = rssGrowth / num;
        std::cout << "Current Rss usage [kb]: " << finalRss / 1024 << std::endl;
        std::cout << "Peak Rss usage [kb]: " << peakRss / 1024 << std::endl;
        std::cout << "Rss growth [kb]: " << rssGrowth / 1024 << std::endl;
        std::cout << "Rss growth per entity [byte]: " << rssGrowthPerEntity << std::endl;
        std::cout << "Rss without db [kb]: " << rssWithoutDb / 1024 << std::endl;
        std::cout << "Percentage peak rss error: " << percentageRssError << std::endl;

        auto onDisk = DummyResource::diskUsage("sink.dummy.instance1");
        auto writeAmplification = static_cast<double>(onDisk) / static_cast<double>(bufferSizeTotal);
        std::cout << "On disk [kb]: " << onDisk / 1024 << std::endl;
        std::cout << "Buffer size total [kb]: " << bufferSizeTotal / 1024 << std::endl;
        std::cout << "Write amplification: " << writeAmplification << std::endl;


        mTimePerEntity << static_cast<double>(allProcessedTime) / static_cast<double>(num);
        mRssGrowthPerEntity << rssGrowthPerEntity;

        QVERIFY(percentageRssError < 10);
        // TODO This is much more than it should it seems, although adding the attachment results in pretty exactly a 1k increase,
        // so it doesn't look like that memory is being duplicated.
        QVERIFY(rssGrowthPerEntity < 2500);

        // HAWD::Dataset dataset("dummy_write_in_process", m_hawdState);
        // HAWD::Dataset::Row row = dataset.row();
        //
        // row.setValue("rows", num);
        // row.setValue("append", (qreal)num/appendTime);
        // row.setValue("total", (qreal)num/allProcessedTime);
        // dataset.insertRow(row);
        // HAWD::Formatter::print(dataset);

        // Print memory layout, RSS is what is in memory
        // std::system("exec pmap -x \"$PPID\"");
    }


private slots:
    void initTestCase()
    {
        Sink::Log::setDebugOutputLevel(Sink::Log::Warning);
    }

    void cleanup()
    {
    }

    void test1k()
    {
        writeInProcess(1000);
    }

    void test2k()
    {
        writeInProcess(2000);
    }

    void test5k()
    {
        writeInProcess(5000);
    }

    // void test20k()
    // {
    //     writeInProcess(20000);
    // }
    //
    void ensureUsedMemoryRemainsStable()
    {
        auto rssStandardDeviation = sqrt(variance(mRssGrowthPerEntity));
        auto timeStandardDeviation = sqrt(variance(mTimePerEntity));
        std::cout << "Rss standard deviation " << rssStandardDeviation << std::endl;
        std::cout << "Rss max difference [byte]" << maxDifference(mRssGrowthPerEntity) << std::endl;
        std::cout << "Time standard deviation " << timeStandardDeviation << std::endl;
        std::cout << "Time max difference [ms]" << maxDifference(mTimePerEntity) << std::endl;
        QVERIFY(rssStandardDeviation < 1000);
        QVERIFY(timeStandardDeviation < 1);
    }

    void getFreePages()
    {
        std::system(QString("mdb_stat %1/%2 -ff").arg(Sink::storageLocation()).arg("sink.dummy.instance1").toLatin1().constData());
    }

    // This allows to run individual parts without doing a cleanup, but still cleaning up normally
    void testCleanupForCompleteTest()
    {
        DummyResource::removeFromDisk("sink.dummy.instance1");
    }

private:
    HAWD::State m_hawdState;
};

QTEST_MAIN(DummyResourceWriteBenchmark)
#include "dummyresourcewritebenchmark.moc"
