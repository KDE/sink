/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
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
    CommandCompletion,
    RevisionUpdateCommand,
    SynchronizeCommand,
    FetchEntityCommand,
    DeleteEntityCommand,
    ModifyEntityCommand,
    CreateEntityCommand,
    SearchSourceCommand, // need a buffer definition for this, but relies on Query API
    CustomCommand = 0xffff
};

void AKONADI2COMMON_EXPORT write(QIODevice *device, int commandId);
void AKONADI2COMMON_EXPORT write(QIODevice *device, int commandId, const char *buffer, uint size);
void AKONADI2COMMON_EXPORT write(QIODevice *device, int commandId, flatbuffers::FlatBufferBuilder &fbb);

}

} // namespace Akonadi2
