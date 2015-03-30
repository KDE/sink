#include <QtTest>

#include "calendar_generated.h"

#include "hawd/dataset.h"
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
        auto summary = fbb.CreateString("summary");
        auto data = fbb.CreateUninitializedVector<uint8_t>(attachmentSize);
        //auto data = fbb.CreateVector(rawData, attachmentSize);
        Calendar::EventBuilder eventBuilder(fbb);
        eventBuilder.add_summary(summary);
        eventBuilder.add_attachment(data);
        auto eventLocation = eventBuilder.Finish();
        Calendar::FinishEventBuffer(fbb, eventLocation);
        memcpy((void*)Calendar::GetEvent(fbb.GetBufferPointer())->attachment()->Data(), rawData, attachmentSize);
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
            for (int i = 0; i < count; i++) {
                if (store) {
                    if (i % 10000 == 0) {
                        if (i > 0) {
                            store->commitTransaction();
                        }
                        store->startTransaction();
                    }

                    store->write(keyPrefix + QByteArray::number(i), event);
                } else {
                    myfile << event.toStdString();
                }
            }

            if (store) {
                store->commitTransaction();
            } else {
                myfile.close();
            }
        }
        qreal writeDuration = time.restart();
        qreal writeOpsPerMs = count / writeDuration;
        qDebug() << "Writing took[ms]: " << writeDuration << "->" << writeOpsPerMs << "ops/ms";

        {
            for (int i = 0; i < count; i++) {
                if (store) {
                    store->scan(keyPrefix + QByteArray::number(i), [](const QByteArray &value) -> bool { return true; });
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
            qDebug() << "Reading took[ms]: " << readDuration << "->" << readOpsPerMs << "ops/ms";
        } else {
            qDebug() << "File reading is not implemented.";
        }
    }

    void testScan()
    {
        QScopedPointer<Akonadi2::Storage> store(new Akonadi2::Storage(testDataPath, dbName, Akonadi2::Storage::ReadOnly));

        QBENCHMARK {
            int hit = 0;
            store->scan("", [&](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
                if (std::string(static_cast<char*>(keyValue), keySize) == "key10000") {
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
            store->scan("key40000", [&](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
                /*
                if (std::string(static_cast<char*>(keyValue), keySize) == "foo") {
                    qDebug() << "hit";
                }
                */
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
        qDebug() << "Creating buffers took[ms]: " << bufferDuration << "->" << opsPerMs << "ops/ms";
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
