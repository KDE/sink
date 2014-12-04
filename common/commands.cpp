#include "commands.h"

#include <QIODevice>

namespace Commands
{

void write(QIODevice *device, int commandId, flatbuffers::FlatBufferBuilder &fbb)
{
    const int dataSize = fbb.GetSize();
    device->write((const char*)&commandId, sizeof(int));
    device->write((const char*)&dataSize, sizeof(int));
    device->write((const char*)fbb.GetBufferPointer(), dataSize);
}

}