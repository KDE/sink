#pragma once

namespace Commands
{

enum CommandIds {
    UnknownCommand = 0,
    HandshakeCommand,
    CustomCommand = 0xffff
};

}