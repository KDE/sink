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

#include "resourceaccess.h"

#include "common/console.h"
#include "common/commands.h"
#include "common/handshake_generated.h"
#include "common/revisionupdate_generated.h"

#include <QDebug>
#include <QDir>
#include <QProcess>

namespace Akonadi2
{

class QueuedCommand
{
public:
    QueuedCommand(int commandId)
        : m_commandId(commandId),
          m_bufferSize(0),
          m_buffer(0)
    {}

    QueuedCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb)
        : m_commandId(commandId),
          m_bufferSize(fbb.GetSize()),
          m_buffer(new char[m_bufferSize])
    {
        memcpy(m_buffer, fbb.GetBufferPointer(), m_bufferSize);
    }

    ~QueuedCommand()
    {
        delete[] m_buffer;
    }

    void write(QIODevice *device)
    {
        Console::main()->log(QString("\tSending queued command %1").arg(m_commandId));
        Commands::write(device, m_commandId, m_buffer, m_bufferSize);
    }

private:
    QueuedCommand(const QueuedCommand &other);
    QueuedCommand &operator=(const QueuedCommand &rhs);

    const int m_commandId;
    const uint m_bufferSize;
    char *m_buffer;
};

class ResourceAccess::Private
{
public:
    Private(const QString &name, ResourceAccess *ra);
    QString resourceName;
    QLocalSocket *socket;
    QTimer *tryOpenTimer;
    bool startingProcess;
    QByteArray partialMessageBuffer;
    flatbuffers::FlatBufferBuilder fbb;
    QVector<QueuedCommand *> commandQueue;
};

ResourceAccess::Private::Private(const QString &name, ResourceAccess *q)
    : resourceName(name),
      socket(new QLocalSocket(q)),
      tryOpenTimer(new QTimer(q)),
      startingProcess(false)
{
}

ResourceAccess::ResourceAccess(const QString &resourceName, QObject *parent)
    : QObject(parent),
      d(new Private(resourceName, this))
{
    d->tryOpenTimer->setInterval(50);
    d->tryOpenTimer->setSingleShot(true);
    connect(d->tryOpenTimer, &QTimer::timeout,
            this, &ResourceAccess::open);

    log("Starting access");
    connect(d->socket, &QLocalSocket::connected,
            this, &ResourceAccess::connected);
    connect(d->socket, &QLocalSocket::disconnected,
            this, &ResourceAccess::disconnected);
    connect(d->socket, SIGNAL(error(QLocalSocket::LocalSocketError)),
            this, SLOT(connectionError(QLocalSocket::LocalSocketError)));
    connect(d->socket, &QIODevice::readyRead,
            this, &ResourceAccess::readResourceMessage);
}

ResourceAccess::~ResourceAccess()
{

}

QString ResourceAccess::resourceName() const
{
    return d->resourceName;
}

bool ResourceAccess::isReady() const
{
    return d->socket->isValid();
}

void ResourceAccess::sendCommand(int commandId)
{
    if (isReady()) {
        log(QString("Sending command %1").arg(commandId));
        Commands::write(d->socket, commandId);
    } else {
        d->commandQueue << new QueuedCommand(commandId);
    }
}

void ResourceAccess::sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb)
{
    if (isReady()) {
        log(QString("Sending command %1").arg(commandId));
        Commands::write(d->socket, commandId, fbb);
    } else {
        d->commandQueue << new QueuedCommand(commandId, fbb);
    }
}

void ResourceAccess::open()
{
    if (d->socket->isValid()) {
        log("Socket valid, so not opening again");
        return;
    }

    //TODO: if we try and try and the process does not pick up
    //      we should probably try to start the process again
    d->socket->setServerName(d->resourceName);
    log(QString("Opening %1").arg(d->socket->serverName()));
    //FIXME: race between starting the exec and opening the socket?
    d->socket->open();
}

void ResourceAccess::close()
{
    log(QString("Closing %1").arg(d->socket->fullServerName()));
    d->socket->close();
}

void ResourceAccess::connected()
{
    d->startingProcess = false;

    if (!isReady()) {
        return;
    }

    log(QString("Connected: %1").arg(d->socket->fullServerName()));

    {
        auto name = d->fbb.CreateString(QString::number((long long)this).toLatin1());
        auto command = Akonadi2::CreateHandshake(d->fbb, name);
        Akonadi2::FinishHandshakeBuffer(d->fbb, command);
        Commands::write(d->socket, Commands::HandshakeCommand, d->fbb);
        d->fbb.Clear();
    }

    //TODO: should confirm the commands made it with a response?
    //TODO: serialize instead of blast them all through the socket?
    log(QString("We have %1 queued commands").arg(d->commandQueue.size()));
    for (QueuedCommand *command: d->commandQueue) {
        command->write(d->socket);
        delete command;
    }
    d->commandQueue.clear();

    emit ready(true);
}

void ResourceAccess::disconnected()
{
    d->socket->close();
    log(QString("Disconnected from %1").arg(d->socket->fullServerName()));
    emit ready(false);
    open();
}

void ResourceAccess::connectionError(QLocalSocket::LocalSocketError error)
{
    log(QString("Connection error: %2").arg(error));
    if (d->startingProcess) {
        if (!d->tryOpenTimer->isActive()) {
            d->tryOpenTimer->start();
        }
        return;
    }

    d->startingProcess = true;
    log(QString("Attempting to start resource ") + d->resourceName);
    QStringList args;
    args << d->resourceName;
    if (QProcess::startDetached("akonadi2_synchronizer", args, QDir::homePath())) {
        if (!d->tryOpenTimer->isActive()) {
            d->tryOpenTimer->start();
        }
    }
}

void ResourceAccess::readResourceMessage()
{
    if (!d->socket->isValid()) {
        return;
    }

    d->partialMessageBuffer += d->socket->readAll();

    // should be scheduled rather than processed all at once
    while (processMessageBuffer()) {}
}

bool ResourceAccess::processMessageBuffer()
{
    static const int headerSize = (sizeof(int) * 2);
    if (d->partialMessageBuffer.size() < headerSize) {
        return false;
    }

    const int commandId = *(int*)d->partialMessageBuffer.constData();
    const int size = *(int*)(d->partialMessageBuffer.constData() + sizeof(int));

    if (size > d->partialMessageBuffer.size() - headerSize) {
        return false;
    }

    switch (commandId) {
        case Commands::RevisionUpdateCommand: {
            auto buffer = Akonadi2::GetRevisionUpdate(d->partialMessageBuffer.constData() + headerSize);
            log(QString("Revision updated to: %1").arg(buffer->revision()));
            emit revisionChanged(buffer->revision());
            break;
        }
        default:
            break;
    }

    d->partialMessageBuffer.remove(0, headerSize + size);
    return d->partialMessageBuffer.size() >= headerSize;
}

void ResourceAccess::log(const QString &message)
{
    Console::main()->log(d->resourceName + ": " + message);
}

}
