/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 * Copyright (C) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "commands.h"

#include <QLocalSocket>
#include <log.h>

namespace Sink {

namespace Commands {

QByteArray name(int commandId)
{
    switch (commandId) {
        case UnknownCommand:
            return "Unknown";
        case CommandCompletionCommand:
            return "Completion";
        case HandshakeCommand:
            return "Handshake";
        case RevisionUpdateCommand:
            return "RevisionUpdate";
        case SynchronizeCommand:
            return "Synchronize";
        case DeleteEntityCommand:
            return "DeleteEntity";
        case ModifyEntityCommand:
            return "ModifyEntity";
        case CreateEntityCommand:
            return "CreateEntity";
        case SearchSourceCommand:
            return "SearchSource";
        case ShutdownCommand:
            return "Shutdown";
        case NotificationCommand:
            return "Notification";
        case PingCommand:
            return "Ping";
        case RevisionReplayedCommand:
            return "RevisionReplayed";
        case InspectionCommand:
            return "Inspection";
        case RemoveFromDiskCommand:
            return "RemoveFromDisk";
        case FlushCommand:
            return "Flush";
        case SecretCommand:
            return "Secret";
        case UpgradeCommand:
            return "Upgrade";
        case CustomCommand:
            return "Custom";
    };
    return QByteArray("Invalid commandId");
}

int headerSize()
{
    return sizeof(int) + (sizeof(uint) * 2);
}

void write(QLocalSocket *device, int messageId, int commandId)
{
    write(device, messageId, commandId, nullptr, 0);
}

static void write(QLocalSocket *device, const char *buffer, uint size)
{
    if (device->write(buffer, size) < 0) {
        SinkWarningCtx(Sink::Log::Context{"commands"}) << "Error while writing " << device->errorString();
    }
}

void write(QLocalSocket *device, int messageId, int commandId, const char *buffer, uint size)
{
    if (size > 0 && !buffer) {
        size = 0;
    }

    write(device, (const char *)&messageId, sizeof(int));
    write(device, (const char *)&commandId, sizeof(int));
    write(device, (const char *)&size, sizeof(uint));
    if (buffer) {
        write(device, buffer, size);
    }
    //The default implementation will happily buffer 200k bytes before sending it out which doesn't make the sytem exactly responsive.
    //1k is arbitrary, but fits a bunch of messages at least.
    if (device->bytesToWrite() > 1000) {
        device->flush();
    }
}

void write(QLocalSocket *device, int messageId, int commandId, flatbuffers::FlatBufferBuilder &fbb)
{
    write(device, messageId, commandId, (const char *)fbb.GetBufferPointer(), fbb.GetSize());
}

} // namespace Commands

} // namespace Sink
