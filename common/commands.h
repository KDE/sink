#pragma once

namespace Commands
{

enum CommandIds {
    UnknownCommand = 0,
    HandshakeCommand,
    RevisionUpdateCommand,
    CustomCommand = 0xffff
};

}