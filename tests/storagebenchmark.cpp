#include <QtTest>

#include "calendar_generated.h"

#include <iostream>
#include <fstream>

#include <QDebug>
#include <QString>
#include <QTime>

#include "common/storage.h"

using namespace Calendar;
using namespace flatbuffers;

static std::string createEvent()
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

    return std::string(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
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
        Storage store(testDataPath, dbName);
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

        QScopedPointer<Storage> store;
        if (useDb) {
            store.reset(new Storage(testDataPath, dbName, Storage::ReadWrite));
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

                    store->write(keyPrefix + std::to_string(i), event);
                } else {
                    myfile << event;
                }
            }

            if (store) {
                store->commitTransaction();
            } else {
                myfile.close();
            }
        }
        qreal writeDuration = time.restart();
        qreal opsPerMs = count / writeDuration;
        qDebug() << "Writing took[ms]: " << writeDuration << "->" << opsPerMs << "ops/ms";

        {
            for (int i = 0; i < count; i++) {
                if (store) {
                    store->read(keyPrefix + std::to_string(i), [](std::string value) -> bool { return true; });
                }
            }
        }
        qreal readDuration = time.restart();
        opsPerMs = count / readDuration;

        if (store) {
            qDebug() << "Reading took[ms]: " << readDuration << "->" << opsPerMs << "ops/ms";
        } else {
            qDebug() << "File reading is not implemented.";
        }
    }
    
    void testScan()
    {
        QScopedPointer<Storage> store(new Storage(testDataPath, dbName, Storage::ReadOnly));

        QBENCHMARK {
            int hit = 0;
            store->scan("", [&](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
                if (std::string(static_cast<char*>(keyValue), keySize) == "key10000") {
                    qDebug() << "hit";
                    hit++;
                }
                return true;
            });
            QCOMPARE(hit, 1);
        }
    }

    void testKeyLookup()
    {
        QScopedPointer<Storage> store(new Storage(testDataPath, dbName, Storage::ReadOnly));

        QBENCHMARK {
            int hit = 0;
            store->scan("key40000", [&](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
                if (std::string(static_cast<char*>(keyValue), keySize) == "foo") {
                    qDebug() << "hit";
                }
                hit++;
                return true;
            });
            QCOMPARE(hit, 1);
        }
    }

    void testBufferCreation()
    {
        QTime time;
        time.start();

        for (int i = 0; i < count; i++) {
            auto event = createEvent();
        }

        qreal bufferDuration = time.restart();
        qreal opsPerMs = count / bufferDuration;
        qDebug() << "Creating buffers took[ms]: " << bufferDuration << "->" << opsPerMs << "ops/ms";;
    }

    void testSizes()
    {
        Storage store(testDataPath, dbName);
        qDebug() << "Database size [kb]: " << store.diskUsage()/1024;

        QFileInfo fileInfo(filePath);
        qDebug() << "File size [kb]: " << fileInfo.size()/1024;
    }
};

QTEST_MAIN(StorageBenchmark)
#include "storagebenchmark.moc"
