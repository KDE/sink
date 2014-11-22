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

std::string createEvent(bool createAttachment = false)
{
    FlatBufferBuilder fbb;
    {
        auto summary = fbb.CreateString("summary");

        // const int attachmentSize = 1024 * 1024; // 1MB
        const int attachmentSize = 1024*2; // 1KB
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

void readEvent(const std::string &data)
{
    auto readEvent = GetEvent(data.c_str());
    std::cout << readEvent->summary()->c_str() << std::endl;
}

int main(int argc, char **argv)
{
    Database db;
    const int count = 50000;
    QTime time;
    time.start();
    // std::ofstream myfile;
    // myfile.open ("buffer.fb");
    //
    auto transaction = db.startTransaction();
    for (int i = 0; i < count; i++) {
        const auto key = QString("key%1").arg(i);
        auto event = createEvent(true);
        db.write(key.toStdString(), event, transaction);

        // myfile << createEvent();
    }
    db.endTransaction(transaction);
    // myfile.close();
    qDebug() << "Writing took: " << time.elapsed();

    time.start();
    for (int i = 0; i < count; i++) {
        const auto key = QString("key%1").arg(i);
        db.read(key.toStdString());
    }
    qDebug() << "Reading took: " << time.elapsed();
}
