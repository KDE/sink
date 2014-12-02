#pragma once

#include <flatbuffers/flatbuffers.h>

class QIODevice;

namespace Commands
{

enum CommandIds {
    UnknownCommand = 0,
    HandshakeCommand,
    RevisionUpdateCommand,
    CustomCommand = 0xffff
};

void write(QIODevice *device, int commandId, flatbuffers::FlatBufferBuilder &fbb);

}