#include <QtTest>

#include "calendar_generated.h"
#include <iostream>
#include <fstream>
#include <QDir>
#include <QString>
#include <QTime>
#include <qdebug.h>

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

static void readEvent(const std::string &data)
{
    auto readEvent = GetEvent(data.c_str());
    std::cout << readEvent->summary()->c_str() << std::endl;
}

class StorageBenchmark : public QObject
{
    Q_OBJECT
private:
    //This should point to a directory on disk and not a ramdisk (since we're measuring performance)
    QString testDataPath;
    QString dbPath;
    QString filePath;

private Q_SLOTS:
    void initTestCase()
    {
        testDataPath = "./";
        dbPath = testDataPath + "testdb";
        filePath = testDataPath + "buffer.fb";

        QDir dir(testDataPath);
        dir.remove("testdb/data.mdb");
        dir.remove("testdb/lock.mdb");
        dir.remove(filePath);
    }

    void testWriteRead_data()
    {
        QTest::addColumn<bool>("useDb");
        QTest::addColumn<int>("count");

        const int count = 50000;
        QTest::newRow("db, 50k") << true << count;
        QTest::newRow("file, 50k") << false << count;
    }

    void testWriteRead()
    {
        QFETCH(bool, useDb);
        QFETCH(int, count);

        Database db(dbPath);

        std::ofstream myfile;
        myfile.open(filePath.toStdString());

        QTime time;
        time.start();
        {
            auto transaction = db.startTransaction();
            for (int i = 0; i < count; i++) {
                const auto key = QString("key%1").arg(i);
                auto event = createEvent();
                if (useDb) {
                    db.write(key.toStdString(), event, transaction);
                } else {
                    myfile << event;
                }
            }
            if (useDb) {
                db.endTransaction(transaction);
            } else {
                myfile.close();
            }
        }
        const int writeDuration = time.elapsed();
        qDebug() << "Writing took[ms]: " << writeDuration;

        time.start();
        {
            for (int i = 0; i < count; i++) {
                const auto key = QString("key%1").arg(i);
                if (useDb) {
                    db.read(key.toStdString());
                }
            }
        }
        const int readDuration = time.elapsed();
        if (useDb) {
            qDebug() << "Reading took[ms]: " << readDuration;
        } else {
            qDebug() << "File reading is not implemented.";
        }
    }

    void testSizes()
    {
        QFileInfo dbInfo(dbPath, "data.mdb");
        QFileInfo fileInfo(filePath);
        qDebug() << "Database size [kb]: " << dbInfo.size()/1024;
        qDebug() << "File size [kb]: " << fileInfo.size()/1024;
    }
};

QTEST_MAIN(StorageBenchmark)
#include "storagebenchmark.moc"
