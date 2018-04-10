#include <QtTest>

#include "dummy_generated.h"

#include "hawd/dataset.h"
#include "hawd/formatter.h"
#include "common/storage.h"
#include "common/log.h"

#include <fstream>

#include <QDebug>
#include <QString>
#include <QTime>

using namespace Sink::ApplicationDomain::Buffer;
using namespace flatbuffers;

static QByteArray createEntity()
{
    static const size_t attachmentSize = 1024 * 2; // 2KB
    static uint8_t rawData[attachmentSize];
    static FlatBufferBuilder fbb;
    fbb.Clear();
    {
        uint8_t *rawDataPtr = Q_NULLPTR;
        auto summary = fbb.CreateString("summary");
        auto data = fbb.CreateUninitializedVector<uint8_t>(attachmentSize, &rawDataPtr);
        DummyBuilder builder(fbb);
        builder.add_summary(summary);
        builder.add_attachment(data);
        FinishDummyBuffer(fbb, builder.Finish());
        memcpy((void *)rawDataPtr, rawData, attachmentSize);
    }

    return QByteArray::fromRawData(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
}

/**
 * Benchmark the storage implementation.
 */
class StorageBenchmark : public QObject
{
    Q_OBJECT
private:
    // This should point to a directory on disk and not a ramdisk (since we're measuring performance)
    QString testDataPath;
    QString dbName;
    QString filePath;
    const int count = 50000;

private slots:
    void initTestCase()
    {
        Sink::Log::setDebugOutputLevel(Sink::Log::Warning);
        testDataPath = "./testdb";
        dbName = "test";
        filePath = testDataPath + "buffer.fb";
    }

    void cleanupTestCase()
    {
        Sink::Storage::DataStore store(testDataPath, dbName);
        store.removeFromDisk();
    }

    void testWriteRead()
    {
        auto entity = createEntity();

        QScopedPointer<Sink::Storage::DataStore> store(new Sink::Storage::DataStore(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite));

        const char *keyPrefix = "key";

        QTime time;
        time.start();
        // Test db write time
        {
            auto transaction = store->createTransaction(Sink::Storage::DataStore::ReadWrite);
            for (int i = 0; i < count; i++) {
                transaction.openDatabase().write(keyPrefix + QByteArray::number(i), entity);
                if ((i % 10000) == 0) {
                    transaction.commit();
                    transaction = store->createTransaction(Sink::Storage::DataStore::ReadWrite);
                }
            }
            transaction.commit();
        }
        qreal dbWriteDuration = time.restart();
        qreal dbWriteOpsPerMs = count / dbWriteDuration;

        // Test file write time
        {
            std::ofstream myfile;
            myfile.open(filePath.toStdString());
            for (int i = 0; i < count; i++) {
                myfile << entity.toStdString();
            }
            myfile.close();
        }
        qreal fileWriteDuration = time.restart();
        qreal fileWriteOpsPerMs = count / fileWriteDuration;

        // Db read time
        {
            auto transaction = store->createTransaction(Sink::Storage::DataStore::ReadOnly);
            auto db = transaction.openDatabase();
            for (int i = 0; i < count; i++) {
                db.scan(keyPrefix + QByteArray::number(i), [](const QByteArray &key, const QByteArray &value) -> bool { return true; });
            }
        }
        qreal readDuration = time.restart();
        qreal readOpsPerMs = count / readDuration;

        HAWD::Dataset dataset("storage_readwrite", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();
        row.setValue("rows", count);
        row.setValue("dbWrite", dbWriteOpsPerMs);
        row.setValue("fileWrite", fileWriteOpsPerMs);
        row.setValue("dbRead", readOpsPerMs);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);

        {
            Sink::Storage::DataStore store(testDataPath, dbName);
            QFileInfo fileInfo(filePath);

            HAWD::Dataset dataset("storage_sizes", m_hawdState);
            HAWD::Dataset::Row row = dataset.row();
            row.setValue("rows", count);
            row.setValue("dbSize", store.diskUsage() / 1024);
            row.setValue("fileSize", fileInfo.size() / 1024);
            dataset.insertRow(row);
            HAWD::Formatter::print(dataset);
        }
    }

    void testScan()
    {
        QScopedPointer<Sink::Storage::DataStore> store(new Sink::Storage::DataStore(testDataPath, dbName, Sink::Storage::DataStore::ReadOnly));

        QBENCHMARK {
            int hit = 0;
            store->createTransaction(Sink::Storage::DataStore::ReadOnly)
                .openDatabase()
                .scan("", [&](const QByteArray &key, const QByteArray &value) -> bool {
                    if (key == "key10000") {
                        // qDebug() << "hit";
                        hit++;
                    }
                    return true;
                });
            QCOMPARE(hit, 1);
        }
    }

    void testKeyLookup()
    {
        QScopedPointer<Sink::Storage::DataStore> store(new Sink::Storage::DataStore(testDataPath, dbName, Sink::Storage::DataStore::ReadOnly));
        auto transaction = store->createTransaction(Sink::Storage::DataStore::ReadOnly);
        auto db = transaction.openDatabase();

        QBENCHMARK {
            int hit = 0;
            db.scan("key40000", [&](const QByteArray &key, const QByteArray &value) -> bool {
                hit++;
                return true;
            });
            QCOMPARE(hit, 1);
        }
    }

    void testFindLatest()
    {
        QScopedPointer<Sink::Storage::DataStore> store(new Sink::Storage::DataStore(testDataPath, dbName, Sink::Storage::DataStore::ReadOnly));
        auto transaction = store->createTransaction(Sink::Storage::DataStore::ReadOnly);
        auto db = transaction.openDatabase();

        QBENCHMARK {
            int hit = 0;
            db.findLatest("key40000", [&](const QByteArray &key, const QByteArray &value) -> bool {
                hit++;
                return true;
            });
            QCOMPARE(hit, 1);
        }
    }

    void testBufferCreation()
    {
        HAWD::Dataset dataset("buffer_creation", m_hawdState);
        HAWD::Dataset::Row row = dataset.row();

        QTime time;
        time.start();

        for (int i = 0; i < count; i++) {
            auto entity = createEntity();
            Q_UNUSED(entity);
        }

        qreal bufferDuration = time.restart();
        qreal opsPerMs = count / bufferDuration;
        row.setValue("numBuffers", count);
        row.setValue("time", bufferDuration);
        row.setValue("ops", opsPerMs);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
    }


private:
    HAWD::State m_hawdState;
};

QTEST_MAIN(StorageBenchmark)
#include "storagebenchmark.moc"
