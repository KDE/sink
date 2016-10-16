#include <QtTest>

#include <QString>

#include "testimplementations.h"

#include <common/facade.h>
#include <common/domainadaptor.h>
#include <common/resultprovider.h>
#include <common/synclistresult.h>
#include <common/definitions.h>
#include <common/query.h>
#include <common/store.h>

#include "hawd/dataset.h"
#include "hawd/formatter.h"

#include <iostream>
#include <math.h>

#include "event_generated.h"
#include "getrssusage.h"

/**
 * Benchmark read performance of the facade implementation.
 *
 * The memory used should grow linearly with the number of retrieved entities.
 * The memory used should be independent from the database size, after accounting for the memory mapped db.
 */
class DatabasePopulationAndFacadeQueryBenchmark : public QObject
{
    Q_OBJECT

    QByteArray identifier;
    QList<double> mRssGrowthPerEntity;
    QList<double> mTimePerEntity;
    HAWD::State mHawdState;

    void populateDatabase(int count)
    {
        Sink::Storage::DataStore(Sink::storageLocation(), "identifier", Sink::Storage::DataStore::ReadWrite).removeFromDisk();
        // Setup
        auto domainTypeAdaptorFactory = QSharedPointer<TestEventAdaptorFactory>::create();
        {
            Sink::Storage::DataStore storage(Sink::storageLocation(), identifier, Sink::Storage::DataStore::ReadWrite);
            auto transaction = storage.createTransaction(Sink::Storage::DataStore::ReadWrite);
            auto db = Sink::Storage::DataStore::mainDatabase(transaction, "event");

            int bufferSizeTotal = 0;
            int keysSizeTotal = 0;
            QByteArray attachment;
            attachment.fill('c', 1000);
            for (int i = 0; i < count; i++) {
                auto domainObject = Sink::ApplicationDomain::Event::Ptr::create();
                domainObject->setProperty("uid", "uid");
                domainObject->setProperty("summary", QString("summary%1").arg(i));
                domainObject->setProperty("attachment", attachment);
                flatbuffers::FlatBufferBuilder fbb;
                domainTypeAdaptorFactory->createBuffer(*domainObject, fbb);
                const auto buffer = QByteArray::fromRawData(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
                const auto key = Sink::Storage::DataStore::generateUid();
                db.write(key, buffer);
                bufferSizeTotal += buffer.size();
                keysSizeTotal += key.size();
            }
            transaction.commit();

            transaction = storage.createTransaction(Sink::Storage::DataStore::ReadOnly);
            db = Sink::Storage::DataStore::mainDatabase(transaction, "event");

            auto dataSizeTotal = count * (QByteArray("uid").size() + QByteArray("summary").size() + attachment.size());
            auto size = db.getSize();
            auto onDisk = storage.diskUsage();
            auto writeAmplification = static_cast<double>(onDisk) / static_cast<double>(bufferSizeTotal);
            std::cout << "Database size [kb]: " << size / 1024 << std::endl;
            std::cout << "On disk [kb]: " << onDisk / 1024 << std::endl;
            std::cout << "Buffer size total [kb]: " << bufferSizeTotal / 1024 << std::endl;
            std::cout << "Key size total [kb]: " << keysSizeTotal / 1024 << std::endl;
            std::cout << "Data size total [kb]: " << dataSizeTotal / 1024 << std::endl;
            std::cout << "Write amplification: " << writeAmplification << std::endl;

            // The buffer has an overhead, but with a reasonable attachment size it should be relatively small
            // A write amplification of 2 should be the worst case
            QVERIFY(writeAmplification < 2);
        }
    }

    void testLoad(int count)
    {
        const auto startingRss = getCurrentRSS();

        Sink::Query query;
        query.liveQuery = false;
        query.requestedProperties << "uid"
                                  << "summary";

        // Benchmark
        QTime time;
        time.start();

        auto resultSet = QSharedPointer<Sink::ResultProvider<Sink::ApplicationDomain::Event::Ptr>>::create();
        auto resourceAccess = QSharedPointer<TestResourceAccess>::create();

        QMap<QByteArray, DomainTypeAdaptorFactoryInterface::Ptr> factories;
        Sink::ResourceContext context{identifier, "test", factories};
        context.mResourceAccess = resourceAccess;
        TestResourceFacade facade(context);

        auto ret = facade.load(query);
        ret.first.exec().waitForFinished();
        auto emitter = ret.second;
        QList<Sink::ApplicationDomain::Event::Ptr> list;
        emitter->onAdded([&list](const Sink::ApplicationDomain::Event::Ptr &event) { list << event; });
        bool done = false;
        emitter->onInitialResultSetComplete([&done](const Sink::ApplicationDomain::Event::Ptr &event) { done = true; });
        emitter->fetch(Sink::ApplicationDomain::Event::Ptr());
        QTRY_VERIFY(done);
        QCOMPARE(list.size(), count);

        const auto elapsed = time.elapsed();

        const auto finalRss = getCurrentRSS();
        const auto rssGrowth = finalRss - startingRss;
        // Since the database is memory mapped it is attributted to the resident set size.
        const auto rssWithoutDb = finalRss - Sink::Storage::DataStore(Sink::storageLocation(), identifier, Sink::Storage::DataStore::ReadWrite).diskUsage();
        const auto peakRss = getPeakRSS();
        // How much peak deviates from final rss in percent (should be around 0)
        const auto percentageRssError = static_cast<double>(peakRss - finalRss) * 100.0 / static_cast<double>(finalRss);
        auto rssGrowthPerEntity = rssGrowth / count;

        std::cout << "Loaded " << list.size() << " results." << std::endl;
        std::cout << "The query took [ms]: " << elapsed << std::endl;
        std::cout << "Current Rss usage [kb]: " << finalRss / 1024 << std::endl;
        std::cout << "Peak Rss usage [kb]: " << peakRss / 1024 << std::endl;
        std::cout << "Rss growth [kb]: " << rssGrowth / 1024 << std::endl;
        std::cout << "Rss growth per entity [byte]: " << rssGrowthPerEntity << std::endl;
        std::cout << "Rss without db [kb]: " << rssWithoutDb / 1024 << std::endl;
        std::cout << "Percentage error: " << percentageRssError << std::endl;

        HAWD::Dataset dataset("facade_query", mHawdState);
        HAWD::Dataset::Row row = dataset.row();
        row.setValue("rows", list.size());
        row.setValue("queryResultPerMs", (qreal)list.size() / elapsed);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);

        mTimePerEntity << static_cast<double>(elapsed) / static_cast<double>(count);
        mRssGrowthPerEntity << rssGrowthPerEntity;

        QVERIFY(percentageRssError < 10);
        // TODO This is much more than it should it seems, although adding the attachment results in pretty exactly a 1k increase,
        // so it doesn't look like that memory is being duplicated.
        QVERIFY(rssGrowthPerEntity < 3300);

        // Print memory layout, RSS is what is in memory
        // std::system("exec pmap -x \"$PPID\"");
        // std::system("top -p \"$PPID\" -b -n 1");
    }

private slots:

    void init()
    {
        identifier = "identifier";
    }

    void test1k()
    {
        populateDatabase(1000);
        testLoad(1000);
    }

    void test2k()
    {
        populateDatabase(2000);
        testLoad(2000);
    }

    void test5k()
    {
        populateDatabase(5000);
        testLoad(5000);
    }

    // void test10k()
    // {
    //     populateDatabase(10000);
    //     testLoad(10000);
    // }

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

    void ensureUsedMemoryRemainsStable()
    {
        auto rssStandardDeviation = sqrt(variance(mRssGrowthPerEntity));
        auto timeStandardDeviation = sqrt(variance(mTimePerEntity));
        std::cout << "Rss standard deviation " << rssStandardDeviation << std::endl;
        std::cout << "Rss max difference [byte]" << maxDifference(mRssGrowthPerEntity) << std::endl;
        std::cout << "Time standard deviation " << timeStandardDeviation << std::endl;
        std::cout << "Time max difference [ms]" << maxDifference(mTimePerEntity) << std::endl;
        QVERIFY(rssStandardDeviation < 500);
        QVERIFY(timeStandardDeviation < 1);
    }
};

QTEST_MAIN(DatabasePopulationAndFacadeQueryBenchmark)
#include "databasepopulationandfacadequerybenchmark.moc"
