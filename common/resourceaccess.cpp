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
#include "common/commandcompletion_generated.h"
#include "common/handshake_generated.h"
#include "common/revisionupdate_generated.h"

#include <QCoreApplication>
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

    void write(QIODevice *device, uint messageId)
    {
        // Console::main()->log(QString("\tSending queued command %1").arg(m_commandId));
        Commands::write(device, messageId, m_commandId, m_buffer, m_bufferSize);
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
    QVector<std::function<void()> > synchronizeResultHandler;
    uint messageId;
};

ResourceAccess::Private::Private(const QString &name, ResourceAccess *q)
    : resourceName(name),
      socket(new QLocalSocket(q)),
      tryOpenTimer(new QTimer(q)),
      startingProcess(false),
      messageId(0)
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
        Commands::write(d->socket, ++d->messageId, commandId);
    } else {
        d->commandQueue << new QueuedCommand(commandId);
    }
}

void ResourceAccess::sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb)
{
    if (isReady()) {
        log(QString("Sending command %1").arg(commandId));
        Commands::write(d->socket, ++d->messageId, commandId, fbb);
    } else {
        d->commandQueue << new QueuedCommand(commandId, fbb);
    }
}

void ResourceAccess::synchronizeResource(const std::function<void()> &resultHandler)
{
    sendCommand(Commands::SynchronizeCommand);
    //TODO: this should be implemented as a job, so we don't need to store the result handler as member
    d->synchronizeResultHandler << resultHandler;
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
        auto name = d->fbb.CreateString(QString::number(QCoreApplication::applicationPid()).toLatin1());
        auto command = Akonadi2::CreateHandshake(d->fbb, name);
        Akonadi2::FinishHandshakeBuffer(d->fbb, command);
        Commands::write(d->socket, ++d->messageId, Commands::HandshakeCommand, d->fbb);
        d->fbb.Clear();
    }

    //TODO: should confirm the commands made it with a response?
    //TODO: serialize instead of blast them all through the socket?
    log(QString("We have %1 queued commands").arg(d->commandQueue.size()));
    for (QueuedCommand *command: d->commandQueue) {
        command->write(d->socket, ++d->messageId);
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
    static const int headerSize = Commands::headerSize();
    if (d->partialMessageBuffer.size() < headerSize) {
        return false;
    }

    //messageId is unused, so commented out
    //const uint messageId = *(int*)(d->partialMessageBuffer.constData());
    const int commandId = *(int*)(d->partialMessageBuffer.constData() + sizeof(uint));
    const uint size = *(int*)(d->partialMessageBuffer.constData() + sizeof(int) + sizeof(uint));

    if (size > (uint)(d->partialMessageBuffer.size() - headerSize)) {
        return false;
    }

    switch (commandId) {
        case Commands::RevisionUpdateCommand: {
            auto buffer = GetRevisionUpdate(d->partialMessageBuffer.constData() + headerSize);
            log(QString("Revision updated to: %1").arg(buffer->revision()));
            emit revisionChanged(buffer->revision());

            //FIXME: The result handler should be called on completion of the synchronize command, and not upon arbitrary revision updates.
            for(auto handler : d->synchronizeResultHandler) {
                //FIXME: we should associate the handler with a buffer->id() to avoid prematurely triggering the result handler from a delayed synchronized response (this is relevant for on-demand syncing).
                handler();
            }
            d->synchronizeResultHandler.clear();
            break;
        }
        case Commands::CommandCompletion: {
            auto buffer = GetCommandCompletion(d->partialMessageBuffer.constData() + headerSize);
            log(QString("Command %1 completed %2").arg(buffer->id()).arg(buffer->success() ? "sucessfully" : "unsuccessfully"));
            //TODO: if a queued command, get it out of the queue ... pass on completion ot the relevant objects .. etc
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
    qDebug() << d->resourceName + ": " + message;
    // Console::main()->log(d->resourceName + ": " + message);
}

}
