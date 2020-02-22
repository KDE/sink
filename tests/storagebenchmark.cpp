#include <QtTest>

#include "dummy_generated.h"

#include "hawd/dataset.h"
#include "hawd/formatter.h"
#include "common/storage.h"
#include "common/storage/key.h"
#include "common/log.h"

#include <definitions.h>
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
        // testDataPath = Sink::storageLocation() + "/testdb";
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

    /*
     * This benchmark is supposed to roughly emulate the workload we get during a large sync.
     *
     * Observed behavior is that without a concurrent read-only transaction free pages are mostly reused,
     * but commits start to take longer and longer.
     * With a concurrent read-only transaction free pages are kept from being reused and commits a greatly sped up.
     *
     * NOTE: At least in podman there seems to be a massive performance difference whether we use a path within the home directory,
     * or in the bind-mounted build directory. This is probably because the home directory is a fuse-overlayfs, and the build directory is
     * just the underlying ext4 volume. I suppose the overlayfs is not suitable for this specific workload.
     */
    void testWrite()
    {
        Sink::Storage::DataStore(testDataPath, dbName).removeFromDisk();

        using namespace Sink::Storage;

        auto entity = createEntity();
        qWarning() << "entity size " << entity.size();
        Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite);

        QTime time;
        time.start();
        // Test db write time
        {
            //This read-only transaction will make commits fast but cause a quick buildup of free pages.
            // auto rotransaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
            for (int i = 0; i < count; i++) {
                time.start();
                const auto identifier = Identifier::fromDisplayByteArray(Sink::Storage::DataStore::generateUid());
                const qint64 newRevision = Sink::Storage::DataStore::maxRevision(transaction) + 1;
                const auto key = Sink::Storage::Key(identifier, newRevision);

                DataStore::mainDatabase(transaction, "mail")
                    .write(newRevision, entity,
                        [&](const DataStore::Error &error) { qWarning() << "Failed to write entity" << identifier << newRevision; });

                DataStore::setMaxRevision(transaction, newRevision);
                DataStore::recordRevision(transaction, newRevision, identifier, "mail");
                DataStore::recordUid(transaction, identifier, "mail");

                auto db = transaction.openDatabase("mail.index.messageId" , std::function<void(const Sink::Storage::DataStore::Error &)>(), Sink::Storage::AllowDuplicates);
                db.write("messageid", key.toInternalByteArray(), [&] (const Sink::Storage::DataStore::Error &error) {
                    qWarning() << "Error while writing value" << error;
                });

                if ((i % 100) == 0) {
                    transaction.commit();
                    qWarning() << "Commit " <<  i << time.elapsed() << "[ms]";
                    transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
                    // rotransaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
                    qWarning() << "Free pages " << store.createTransaction(Sink::Storage::DataStore::ReadOnly).stat(false).freePages;
                }
            }
            transaction.commit();
        }
        qreal dbWriteDuration = time.elapsed();
        qreal dbWriteOpsPerMs = count / dbWriteDuration;
        {
            auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadOnly);
            auto stat = transaction.stat(false);
            qWarning() << "Write duration " << dbWriteDuration;
            qWarning() << "free " << stat.freePages;
            qWarning() << "total " << stat.totalPages;

            QList<QByteArray> databases = transaction.getDatabaseNames();
            for (const auto &databaseName : databases) {
                auto db = transaction.openDatabase(databaseName);
                qint64 size = db.getSize() / 1024;
                qWarning() << QString("%1: %2 [kb]").arg(QString(databaseName)).arg(size);
            }
        }
    }

private:
    HAWD::State m_hawdState;
};

QTEST_MAIN(StorageBenchmark)
#include "storagebenchmark.moc"
