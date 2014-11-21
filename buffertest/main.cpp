#include "calendar_generated.h"
#include <iostream>
#include <fstream>

using namespace Calendar;
using namespace flatbuffers;

std::string createEvent()
{
    FlatBufferBuilder fbb;
    {
        auto summary = fbb.CreateString("summary");
        const int attachmentSize = 1024 * 1024; // 1MB
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
    std::ofstream myfile;
    myfile.open ("buffer.fb");
    myfile << createEvent();
    myfile.close();
}
