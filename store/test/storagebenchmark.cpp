#include <QtTest>

#include "calendar_generated.h"

#include <iostream>
#include <fstream>

#include <QDebug>
#include <QString>
#include <QTime>

#include "store/database.h"

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
        Database db(testDataPath, dbName);
        db.removeFromDisk();
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

        Database *db = 0;
        if (useDb) {
            db = new Database(testDataPath, dbName);
        }

        std::ofstream myfile;
        myfile.open(filePath.toStdString());
        const char *keyPrefix = "key";

        QTime time;

        time.start();
        {
            auto event = createEvent();
            for (int i = 0; i < count; i++) {
                if (db) {
                    if (i % 10000 == 0) {
                        if (i > 0) {
                            db->commitTransaction();
                        }
                        db->startTransaction();
                    }

                    db->write(keyPrefix + std::to_string(i), event);
                } else {
                    myfile << event;
                }
            }

            if (db) {
                db->commitTransaction();
            } else {
                myfile.close();
            }
        }
        const int writeDuration = time.restart();
        qDebug() << "Writing took[ms]: " << writeDuration;

        {
            for (int i = 0; i < count; i++) {
                if (db) {
                    db->read(keyPrefix + std::to_string(i), [](std::string value){});
                }
            }
        }
        const int readDuration = time.restart();

        if (db) {
            qDebug() << "Reading took[ms]: " << readDuration;
        } else {
            qDebug() << "File reading is not implemented.";
        }

        delete db;
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
        Database db(testDataPath, dbName);
        qDebug() << "Database size [kb]: " << db.diskUsage()/1024;

        QFileInfo fileInfo(filePath);
        qDebug() << "File size [kb]: " << fileInfo.size()/1024;
    }
};

QTEST_MAIN(StorageBenchmark)
#include "storagebenchmark.moc"
