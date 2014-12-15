#pragma once

#include <akonadi2common_export.h>
#include <flatbuffers/flatbuffers.h>

class QIODevice;

namespace Akonadi2
{

namespace Commands
{

enum CommandIds {
    UnknownCommand = 0,
    HandshakeCommand,
    RevisionUpdateCommand,
    CustomCommand = 0xffff
};

void AKONADI2COMMON_EXPORT write(QIODevice *device, int commandId, flatbuffers::FlatBufferBuilder &fbb);

}

} // namespace Akonadi2
