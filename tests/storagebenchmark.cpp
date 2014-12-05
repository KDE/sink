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
    FlatBufferBuilder fbb;
    {
        auto summary = fbb.CreateString("summary");

        const int attachmentSize = 1024*2; // 2KB
        int8_t rawData[attachmentSize];
        auto data = fbb.CreateVector(rawData, attachmentSize);

        Calendar::EventBuilder eventBuilder(fbb);
        eventBuilder.add_summary(summary);
        eventBuilder.add_attachment(data);
        auto eventLocation = eventBuilder.Finish();
        FinishEventBuffer(fbb, eventLocation);
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

        Storage *store = 0;
        if (useDb) {
            store = new Storage(testDataPath, dbName, Storage::ReadWrite);
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
        const int writeDuration = time.restart();
        qDebug() << "Writing took[ms]: " << writeDuration;

        {
            for (int i = 0; i < count; i++) {
                if (store) {
                    store->read(keyPrefix + std::to_string(i), [](std::string value) -> bool { return true; });
                }
            }
        }
        const int readDuration = time.restart();

        if (store) {
            qDebug() << "Reading took[ms]: " << readDuration;
        } else {
            qDebug() << "File reading is not implemented.";
        }

        delete store;
    }

    void testBufferCreation()
    {
        QTime time;

        time.start();
        {
            for (int i = 0; i < count; i++) {
                auto event = createEvent();
            }
        }
        const int bufferDuration = time.elapsed();
        qDebug() << "Creating buffers took[ms]: " << bufferDuration;
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
