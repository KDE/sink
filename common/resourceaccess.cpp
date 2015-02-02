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
#include "common/synchronize_generated.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QProcess>

namespace Akonadi2
{

class QueuedCommand
{
public:
    QueuedCommand(int commandId, const std::function<void(int, const QString &)> &callback)
        : commandId(commandId),
          bufferSize(0),
          buffer(0),
          callback(callback)
    {}

    QueuedCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb, const std::function<void(int, const QString &)> &callback)
        : commandId(commandId),
          bufferSize(fbb.GetSize()),
          buffer(new char[bufferSize]),
          callback(callback)
    {
        memcpy(buffer, fbb.GetBufferPointer(), bufferSize);
    }

    ~QueuedCommand()
    {
        delete[] buffer;
    }

private:
    QueuedCommand(const QueuedCommand &other);
    QueuedCommand &operator=(const QueuedCommand &rhs);

public:
    const int commandId;
    const uint bufferSize;
    char *buffer;
    std::function<void(int, const QString &)> callback;
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
    QMultiMap<uint, std::function<void(int error, const QString &errorMessage)> > resultHandler;
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

void ResourceAccess::registerCallback(uint messageId, const std::function<void(int error, const QString &errorMessage)> &callback)
{
    d->resultHandler.insert(messageId, callback);
}

Async::Job<void> ResourceAccess::sendCommand(int commandId)
{
    return Async::start<void>([this, commandId](Async::Future<void> &f) {
        auto continuation = [&f](int error, const QString &errorMessage) {
            if (error) {
                f.setError(error, errorMessage);
            }
            f.setFinished();
        };
        if (isReady()) {
            log(QString("Sending command %1").arg(commandId));
            d->messageId++;
            registerCallback(d->messageId, continuation);
            Commands::write(d->socket, d->messageId, commandId);
        } else {
            d->commandQueue << new QueuedCommand(commandId, continuation);
        }
    });
}

/*
 * TODO JOBAPI: This is a workaround to be able to return a job below to
 * may or may not already be finished when the job is started. The job API should provide a mechanism
 * for this. Essentially we need a way to set a job finished externally (we use the finisher as handle for that).
 * If the job is then started the continuation should immediately be executed if the job finished already, and otherwise
 * just wait until the work is done, and then execute the continuation as usual.
 */
struct JobFinisher {
    bool finished;
    std::function<void(int error, const QString &errorMessage)> callback;

    JobFinisher() : finished(false) {}

    void setFinished(int error, const QString &errorMessage) {
        finished = true;
        if (callback) {
            callback(error, errorMessage);
        }
    }
};

Async::Job<void>  ResourceAccess::sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb)
{
    auto finisher = QSharedPointer<JobFinisher>::create();
    auto callback = [finisher] (int error, const QString &errorMessage) {
        finisher->setFinished(error, errorMessage);
    };
    if (isReady()) {
        d->messageId++;
        log(QString("Sending command %1 with messageId %2").arg(commandId).arg(d->messageId));
        registerCallback(d->messageId, callback);
        Commands::write(d->socket, d->messageId, commandId, fbb);
    } else {
        d->commandQueue << new QueuedCommand(commandId, fbb, callback);
    }
    return Async::start<void>([this, finisher](Async::Future<void> &f) {
        if (finisher->finished) {
            f.setFinished();
        } else {
            finisher->callback = [&f](int error, const QString &errorMessage) {
                if (error) {
                    f.setError(error, errorMessage);
                }
                f.setFinished();
            };
        }
    });
}

Async::Job<void> ResourceAccess::synchronizeResource(bool sourceSync, bool localSync)
{
    auto command = Akonadi2::CreateSynchronize(d->fbb, sourceSync, localSync);
    Akonadi2::FinishSynchronizeBuffer(d->fbb, command);
    return sendCommand(Commands::SynchronizeCommand, d->fbb);
    d->fbb.Clear();
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
        d->messageId++;
        log(QString("Sending command %1 with messageId %2").arg(command->commandId).arg(d->messageId));
        if (command->callback) {
            registerCallback(d->messageId, command->callback);
        }
        Commands::write(d->socket, d->messageId, command->commandId, command->buffer, command->bufferSize);
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
    if (d->startingProcess) {
        if (!d->tryOpenTimer->isActive()) {
            d->tryOpenTimer->start();
        }
        return;
    }
    log(QString("Connection error: %1 : %2").arg(error).arg(d->socket->errorString()));
    if (error == QLocalSocket::PeerClosedError) {
        log("The resource closed the connection. It probably crashed.");
    }

    for(auto handler : d->resultHandler.values()) {
        handler(1, "The resource closed unexpectedly");
    }
    d->resultHandler.clear();

    d->startingProcess = true;
    log(QString("Attempting to start resource ") + d->resourceName);
    QStringList args;
    args << d->resourceName;
    if (QProcess::startDetached("akonadi2_synchronizer", args, QDir::homePath())) {
        if (!d->tryOpenTimer->isActive()) {
            d->tryOpenTimer->start();
        }
    } else {
        qWarning() << "Failed to start resource";
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
        qWarning() << "command too small";
        return false;
    }

    //const uint messageId = *(int*)(d->partialMessageBuffer.constData());
    const int commandId = *(int*)(d->partialMessageBuffer.constData() + sizeof(uint));
    const uint size = *(int*)(d->partialMessageBuffer.constData() + sizeof(int) + sizeof(uint));

    if (size > (uint)(d->partialMessageBuffer.size() - headerSize)) {
        qWarning() << "command too small";
        return false;
    }

    switch (commandId) {
        case Commands::RevisionUpdateCommand: {
            auto buffer = GetRevisionUpdate(d->partialMessageBuffer.constData() + headerSize);
            log(QString("Revision updated to: %1").arg(buffer->revision()));
            emit revisionChanged(buffer->revision());

            break;
        }
        case Commands::CommandCompletion: {
            auto buffer = GetCommandCompletion(d->partialMessageBuffer.constData() + headerSize);
            log(QString("Command with messageId %1 completed %2").arg(buffer->id()).arg(buffer->success() ? "sucessfully" : "unsuccessfully"));
            //TODO: if a queued command, get it out of the queue ... pass on completion ot the relevant objects .. etc

            //The callbacks can result in this object getting destroyed directly, so we need to ensure we finish our work first
            QMetaObject::invokeMethod(this, "callCallbacks", Qt::QueuedConnection, QGenericReturnArgument(), Q_ARG(int, buffer->id()));
            break;
        }
        default:
            break;
    }

    d->partialMessageBuffer.remove(0, headerSize + size);
    return d->partialMessageBuffer.size() >= headerSize;
}

void ResourceAccess::callCallbacks(int id)
{
    for(auto handler : d->resultHandler.values(id)) {
        handler(0, QString());
    }
    d->resultHandler.remove(id);
}

void ResourceAccess::log(const QString &message)
{
    qDebug() << d->resourceName + ": " + message;
    // Console::main()->log(d->resourceName + ": " + message);
}

}
