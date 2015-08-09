#include <QtTest>

#include "calendar_generated.h"

#include "hawd/dataset.h"
#include "hawd/formatter.h"
#include "common/storage.h"

#include <iostream>
#include <fstream>

#include <QDebug>
#include <QString>
#include <QTime>

using namespace Calendar;
using namespace flatbuffers;

static QByteArray createEvent()
{
    static const size_t attachmentSize = 1024*2; // 2KB
    static uint8_t rawData[attachmentSize];
    static FlatBufferBuilder fbb;
    fbb.Clear();
    {
        uint8_t *rawDataPtr = Q_NULLPTR;
        auto summary = fbb.CreateString("summary");
        auto data = fbb.CreateUninitializedVector<uint8_t>(attachmentSize, &rawDataPtr);
        //auto data = fbb.CreateVector(rawData, attachmentSize);
        Calendar::EventBuilder eventBuilder(fbb);
        eventBuilder.add_summary(summary);
        eventBuilder.add_attachment(data);
        auto eventLocation = eventBuilder.Finish();
        Calendar::FinishEventBuffer(fbb, eventLocation);
        memcpy((void*)rawDataPtr, rawData, attachmentSize);
    }

    return QByteArray::fromRawData(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
}

// static void readEvent(const std::string &data)
// {
//     auto readEvent = GetEvent(data.c_str());
//     std::cout << readEvent->summary()->c_str() << std::endl;
// }

class StorageBenchmark : public QObject
{
    Q_OBJECT
private:
    //This should point to a directory on disk and not a ramdisk (since we're measuring performance)
    QString testDataPath;
    QString dbName;
    QString filePath;
    const int count = 50000;

private Q_SLOTS:
    void initTestCase()
    {
        testDataPath = "./testdb";
        dbName = "test";
        filePath = testDataPath + "buffer.fb";
    }

    void cleanupTestCase()
    {
        Akonadi2::Storage store(testDataPath, dbName);
        store.removeFromDisk();
    }

    void testWriteRead_data()
    {
        QTest::addColumn<bool>("useDb");
        QTest::addColumn<int>("count");

        QTest::newRow("db, 50k") << true << count;
        QTest::newRow("file, 50k") << false << count;
    }

    void testWriteRead()
    {
        QFETCH(bool, useDb);
        QFETCH(int, count);

        QScopedPointer<Akonadi2::Storage> store;
        if (useDb) {
            store.reset(new Akonadi2::Storage(testDataPath, dbName, Akonadi2::Storage::ReadWrite));
        }

        std::ofstream myfile;
        myfile.open(filePath.toStdString());
        const char *keyPrefix = "key";

        QTime time;
        time.start();
        {
            auto event = createEvent();
            if (store) {
                auto transaction = store->createTransaction(Akonadi2::Storage::ReadWrite);
                for (int i = 0; i < count; i++) {
                    if (i % 10000 == 0) {
                        if (i > 0) {
                            transaction.commit();
                            transaction = std::move(store->createTransaction(Akonadi2::Storage::ReadWrite));
                        }
                    }

                    transaction.write(keyPrefix + QByteArray::number(i), event);
                }
                transaction.commit();
            } else {
                for (int i = 0; i < count; i++) {
                    myfile << event.toStdString();
                }
                myfile.close();
            }
        }
        qreal writeDuration = time.restart();
        qreal writeOpsPerMs = count / writeDuration;
        qDebug() << "Writing took[ms]: " << writeDuration << "->" << writeOpsPerMs << "ops/ms";

        {
            if (store) {
                auto transaction = store->createTransaction(Akonadi2::Storage::ReadOnly);
                for (int i = 0; i < count; i++) {
                    transaction.scan(keyPrefix + QByteArray::number(i), [](const QByteArray &key, const QByteArray &value) -> bool { return true; });
                }
            }
        }
        qreal readDuration = time.restart();
        qreal readOpsPerMs = count / readDuration;

        if (store) {
            HAWD::Dataset dataset("storage_readwrite", m_hawdState);
            HAWD::Dataset::Row row = dataset.row();
            row.setValue("rows", count);
            row.setValue("write", writeDuration);
            row.setValue("writeOps", writeOpsPerMs);
            row.setValue("read", readOpsPerMs);
            row.setValue("readOps", readOpsPerMs);
            dataset.insertRow(row);
            HAWD::Formatter::print(dataset);
        } else {
            qDebug() << "File reading is not implemented.";
        }
    }

    void testScan()
    {
        QScopedPointer<Akonadi2::Storage> store(new Akonadi2::Storage(testDataPath, dbName, Akonadi2::Storage::ReadOnly));

        QBENCHMARK {
            int hit = 0;
            store->createTransaction(Akonadi2::Storage::ReadOnly).scan("", [&](const QByteArray &key, const QByteArray &value) -> bool {
                if (key == "key10000") {
                    //qDebug() << "hit";
                    hit++;
                }
                return true;
            });
            QCOMPARE(hit, 1);
        }
    }

    void testKeyLookup()
    {
        QScopedPointer<Akonadi2::Storage> store(new Akonadi2::Storage(testDataPath, dbName, Akonadi2::Storage::ReadOnly));

        QBENCHMARK {
            int hit = 0;
            store->createTransaction(Akonadi2::Storage::ReadOnly).scan("key40000", [&](const QByteArray &key, const QByteArray &value) -> bool {
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
            auto event = createEvent();
        }

        qreal bufferDuration = time.restart();
        qreal opsPerMs = count / bufferDuration;
        row.setValue("numBuffers", count);
        row.setValue("time", bufferDuration);
        row.setValue("ops", opsPerMs);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
    }

    void testSizes()
    {
        Akonadi2::Storage store(testDataPath, dbName);
        qDebug() << "Database size [kb]: " << store.diskUsage()/1024;

        QFileInfo fileInfo(filePath);
        qDebug() << "File size [kb]: " << fileInfo.size()/1024;
    }


private:
    HAWD::State m_hawdState;
};

QTEST_MAIN(StorageBenchmark)
#include "storagebenchmark.moc"
