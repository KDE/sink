#include <QtTest>

#include <QString>
#include <QDateTime>

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
#include "mail_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"

#include "getrssusage.h"
#include "utils.h"

#include <KMime/Message>

static QByteArray createEntityBuffer(size_t attachmentSize, int &bufferSize)
{
    flatbuffers::FlatBufferBuilder eventFbb;
    eventFbb.Clear();
    {

        auto msg = KMime::Message::Ptr::create();
        msg->subject()->from7BitString("Some subject");
        msg->setBody("This is the body now.");
        msg->assemble();

        const auto data = msg->encodedContent();

        auto summary = eventFbb.CreateString("summary");
        auto mimeMessage = eventFbb.CreateString(data.constData(), data.length());
        Sink::ApplicationDomain::Buffer::MailBuilder eventBuilder(eventFbb);
        eventBuilder.add_subject(summary);
        eventBuilder.add_messageId(summary);
        eventBuilder.add_mimeMessage(mimeMessage);
        Sink::ApplicationDomain::Buffer::FinishMailBuffer(eventFbb, eventBuilder.Finish());
    }

    flatbuffers::FlatBufferBuilder entityFbb;
    Sink::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, 0, 0, eventFbb.GetBufferPointer(), eventFbb.GetSize());
    bufferSize = entityFbb.GetSize();

    flatbuffers::FlatBufferBuilder fbb;
    auto type = fbb.CreateString(Sink::ApplicationDomain::getTypeName<Sink::ApplicationDomain::Mail>().toStdString().data());
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
    QDateTime mTimeStamp{QDateTime::currentDateTimeUtc()};

    void writeInProcess(int num, const QDateTime &timestamp)
    {
        DummyResource::removeFromDisk("sink.dummy.instance1");

        QTime time;
        time.start();
        DummyResource resource(Sink::ResourceContext{"sink.dummy.instance1", "sink.dummy", Sink::AdaptorFactoryRegistry::instance().getFactories("sink.dummy")});

        int bufferSize = 0;
        auto command = createEntityBuffer(0, bufferSize);

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

        auto onDisk = Sink::Storage::DataStore(Sink::storageLocation(), "sink.dummy.instance1", Sink::Storage::DataStore::ReadOnly).diskUsage();
        auto writeAmplification = static_cast<double>(onDisk) / static_cast<double>(bufferSizeTotal);
        std::cout << "On disk [kb]: " << onDisk / 1024 << std::endl;
        std::cout << "Buffer size total [kb]: " << bufferSizeTotal / 1024 << std::endl;
        std::cout << "Write amplification: " << writeAmplification << std::endl;


        mTimePerEntity << static_cast<double>(allProcessedTime) / static_cast<double>(num);
        mRssGrowthPerEntity << rssGrowthPerEntity;

        {
            HAWD::Dataset dataset("dummy_write_perf", m_hawdState);
            HAWD::Dataset::Row row = dataset.row();
            row.setValue("rows", num);
            row.setValue("append", (qreal)num/appendTime);
            row.setValue("total", (qreal)num/allProcessedTime);
            row.setTimestamp(timestamp);
            dataset.insertRow(row);
            HAWD::Formatter::print(dataset);
        }

        {
            HAWD::Dataset dataset("dummy_write_memory", m_hawdState);
            HAWD::Dataset::Row row = dataset.row();
            row.setValue("rows", num);
            row.setValue("rss", QVariant::fromValue(finalRss / 1024));
            row.setValue("peakRss", QVariant::fromValue(peakRss / 1024));
            row.setValue("percentagePeakRssError", percentageRssError);
            row.setValue("rssGrowthPerEntity", QVariant::fromValue(rssGrowthPerEntity));
            row.setValue("rssWithoutDb", rssWithoutDb / 1024);
            row.setTimestamp(timestamp);
            dataset.insertRow(row);
            HAWD::Formatter::print(dataset);
        }

        {
            HAWD::Dataset dataset("dummy_write_disk", m_hawdState);
            HAWD::Dataset::Row row = dataset.row();
            row.setValue("rows", num);
            row.setValue("onDisk", onDisk / 1024);
            row.setValue("bufferSize", bufferSizeTotal / 1024);
            row.setValue("writeAmplification", writeAmplification);
            row.setTimestamp(timestamp);
            dataset.insertRow(row);
            HAWD::Formatter::print(dataset);
        }

        // Print memory layout, RSS is what is in memory
        // std::system("exec pmap -x \"$PPID\"");
    }

    void testDiskUsage(int num)
    {
        auto resourceId = "testDiskUsage";
        DummyResource::removeFromDisk(resourceId);

        {
            DummyResource resource(Sink::ResourceContext{resourceId, "sink.dummy", Sink::AdaptorFactoryRegistry::instance().getFactories("sink.dummy")});

            int bufferSize = 0;
            auto command = createEntityBuffer(1000, bufferSize);

            for (int i = 0; i < num; i++) {
                resource.processCommand(Sink::Commands::CreateEntityCommand, command);
            }

            // Wait until all messages have been processed
            resource.processAllMessages().exec().waitForFinished();
        }

        qint64 totalDbSizes = 0;
        qint64 totalKeysAndValues = 0;
        QMap<QByteArray, qint64> dbSizes;
        Sink::Storage::DataStore storage(Sink::storageLocation(), resourceId, Sink::Storage::DataStore::ReadOnly);
        auto transaction = storage.createTransaction(Sink::Storage::DataStore::ReadOnly);
        auto stat = transaction.stat();

        std::cout << "Free pages: " << stat.freePages << std::endl;
        std::cout << "Total pages: " << stat.totalPages << std::endl;
        auto totalUsedSize = stat.pageSize * (stat.totalPages - stat.freePages);
        std::cout << "Used size: " << totalUsedSize << std::endl;

        auto freeDbSize = stat.pageSize * (stat.freeDbStat.leafPages + stat.freeDbStat.overflowPages + stat.freeDbStat.branchPages);
        std::cout << "Free db size: " << freeDbSize << std::endl;
        auto mainDbSize = stat.pageSize * (stat.mainDbStat.leafPages + stat.mainDbStat.overflowPages + stat.mainDbStat.branchPages);
        std::cout << "Main db size: " << mainDbSize << std::endl;

        totalDbSizes += mainDbSize;
        QList<QByteArray> databases = transaction.getDatabaseNames();
        for (const auto &databaseName : databases) {
            auto db = transaction.openDatabase(databaseName);
            const auto size = db.getSize();
            dbSizes.insert(databaseName, size);
            totalDbSizes += size;

            qint64 keySizes = 0;
            qint64 valueSizes = 0;
            db.scan({}, [&] (const QByteArray &key, const QByteArray &data) {
                    keySizes += key.size();
                    valueSizes += data.size();
                    return true;
                },
                [&](const Sink::Storage::DataStore::Error &e) {
                    qWarning() << "Error while reading" << e;
                },
                false, false);

            auto s = db.stat();
            auto usedPages = (s.leafPages + s.branchPages + s.overflowPages);

            std::cout << std::endl;
            std::cout << "Db: " << databaseName.toStdString() << (db.allowsDuplicates() ? " DUP" : "") << std::endl;
            std::cout << "Used pages " << usedPages << std::endl;
            std::cout << "Used size " << (keySizes + valueSizes) / 4096.0 << std::endl;
            std::cout << "Entries " << s.numEntries << std::endl;
            totalKeysAndValues += (keySizes + valueSizes);
        }
        std::cout << std::endl;

        auto mainStoreOnDisk = Sink::Storage::DataStore(Sink::storageLocation(), resourceId, Sink::Storage::DataStore::ReadOnly).diskUsage();
        auto totalOnDisk = DummyResource::diskUsage(resourceId);
        std::cout << "Calculated key + value size: " << totalKeysAndValues << std::endl;
        std::cout << "Calculated total db sizes: " << totalDbSizes << std::endl;
        std::cout << "Main store on disk: " << mainStoreOnDisk << std::endl;
        std::cout << "Total on disk: " << totalOnDisk << std::endl;
        std::cout << "Used size amplification: " << static_cast<double>(totalUsedSize) / static_cast<double>(totalKeysAndValues) << std::endl;
        std::cout << "Write amplification: " << static_cast<double>(mainStoreOnDisk) / static_cast<double>(totalKeysAndValues) << std::endl;
        std::cout << std::endl;
    }

private slots:
    void initTestCase()
    {
        Sink::Log::setDebugOutputLevel(Sink::Log::Warning);
        auto factory = Sink::ResourceFactory::load("sink.dummy");
        QVERIFY(factory);
    }

    void cleanup()
    {
    }

    void runBenchmarks()
    {
        writeInProcess(5000, mTimeStamp);
    }

    void ensureUsedMemoryRemainsStable()
    {
        auto rssStandardDeviation = sqrt(variance(mRssGrowthPerEntity));
        auto timeStandardDeviation = sqrt(variance(mTimePerEntity));
        HAWD::Dataset dataset("dummy_write_summary", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();
        row.setValue("rssStandardDeviation", rssStandardDeviation);
        row.setValue("rssMaxDifference", maxDifference(mRssGrowthPerEntity));
        row.setValue("timeStandardDeviation", timeStandardDeviation);
        row.setValue("timeMaxDifference", maxDifference(mTimePerEntity));
        row.setTimestamp(mTimeStamp);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
    }

    void testDiskUsage()
    {
        testDiskUsage(1000);
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
