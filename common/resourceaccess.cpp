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

#include "common/commands.h"
#include "common/commandcompletion_generated.h"
#include "common/handshake_generated.h"
#include "common/revisionupdate_generated.h"
#include "common/synchronize_generated.h"
#include "common/notification_generated.h"
#include "log.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QProcess>

#undef Trace
#define Trace() debugStream(DebugLevel::Trace, __LINE__, __FILE__, Q_FUNC_INFO, "ResourceAccess")
#undef Log
#define Log(IDENTIFIER) debugStream(DebugLevel::Log, __LINE__, __FILE__, Q_FUNC_INFO, "ResourceAccess("+IDENTIFIER+")")

namespace Akonadi2
{

class QueuedCommand
{
public:
    QueuedCommand(int commandId, const std::function<void(int, const QString &)> &callback)
        : commandId(commandId),
          callback(callback)
    {}

    QueuedCommand(int commandId, const QByteArray &b, const std::function<void(int, const QString &)> &callback)
        : commandId(commandId),
          buffer(b),
          callback(callback)
    {
    }

private:
    QueuedCommand(const QueuedCommand &other);
    QueuedCommand &operator=(const QueuedCommand &rhs);

public:
    const int commandId;
    QByteArray buffer;
    std::function<void(int, const QString &)> callback;
};

class ResourceAccess::Private
{
public:
    Private(const QByteArray &name, const QByteArray &instanceIdentifier, ResourceAccess *ra);
    KAsync::Job<void> tryToConnect();
    KAsync::Job<void> initializeSocket();
    QByteArray resourceName;
    QByteArray resourceInstanceIdentifier;
    QSharedPointer<QLocalSocket> socket;
    QByteArray partialMessageBuffer;
    flatbuffers::FlatBufferBuilder fbb;
    QVector<QSharedPointer<QueuedCommand>> commandQueue;
    QMap<uint, QSharedPointer<QueuedCommand>> pendingCommands;
    QMultiMap<uint, std::function<void(int error, const QString &errorMessage)> > resultHandler;
    uint messageId;
};

ResourceAccess::Private::Private(const QByteArray &name, const QByteArray &instanceIdentifier, ResourceAccess *q)
    : resourceName(name),
      resourceInstanceIdentifier(instanceIdentifier),
      messageId(0)
{
}

//Connects to server and returns connected socket on success
KAsync::Job<QSharedPointer<QLocalSocket> > ResourceAccess::connectToServer(const QByteArray &identifier)
{
    auto s = QSharedPointer<QLocalSocket>::create();
    return KAsync::start<QSharedPointer<QLocalSocket> >([identifier, s](KAsync::Future<QSharedPointer<QLocalSocket> > &future) {
        s->setServerName(identifier);
        auto context = new QObject;
        QObject::connect(s.data(), &QLocalSocket::connected, context, [&future, &s, context]() {
            Q_ASSERT(s);
            delete context;
            future.setValue(s);
            future.setFinished();
        });
        QObject::connect(s.data(), static_cast<void(QLocalSocket::*)(QLocalSocket::LocalSocketError)>(&QLocalSocket::error), context, [&future, context](QLocalSocket::LocalSocketError) {
            delete context;
            future.setError(-1, "Failed to connect to server.");
        });
        s->open();
    });
}

KAsync::Job<void> ResourceAccess::Private::tryToConnect()
{
    return KAsync::dowhile([this]() -> bool {
        //TODO abort after N retries?
        return !socket;
    },
    [this](KAsync::Future<void> &future) {
        Trace() << "Loop";
        KAsync::wait(50)
        .then(connectToServer(resourceInstanceIdentifier))
        .then<void, QSharedPointer<QLocalSocket> >([this, &future](const QSharedPointer<QLocalSocket> &s) {
            Q_ASSERT(s);
            socket = s;
            future.setFinished();
        },
        [&future](int errorCode, const QString &errorString) {
            future.setFinished();
        }).exec();
    });
}

KAsync::Job<void> ResourceAccess::Private::initializeSocket()
{
    return KAsync::start<void>([this](KAsync::Future<void> &future) {
        Trace() << "Trying to connect";
        connectToServer(resourceInstanceIdentifier).then<void, QSharedPointer<QLocalSocket> >([this, &future](const QSharedPointer<QLocalSocket> &s) {
            Trace() << "Connected to resource, without having to start it.";
            Q_ASSERT(s);
            socket = s;
            future.setFinished();
        },
        [this, &future](int errorCode, const QString &errorString) {
            Trace() << "Failed to connect, starting resource";
            //We failed to connect, so let's start the resource
            QStringList args;
            args << resourceInstanceIdentifier;
            qint64 pid = 0;
            if (QProcess::startDetached("akonadi2_synchronizer", args, QDir::homePath(), &pid)) {
                Trace() << "Started resource " << pid;
                tryToConnect()
                .then<void>([&future]() {
                    future.setFinished();
                }).exec();
            } else {
                Warning() << "Failed to start resource";
            }
        }).exec();
    });
}

static QByteArray getResourceName(const QByteArray &instanceIdentifier)
{
    auto split = instanceIdentifier.split('.');
    split.removeLast();
    return split.join('.');
}

ResourceAccess::ResourceAccess(const QByteArray &resourceInstanceIdentifier, QObject *parent)
    : QObject(parent),
      d(new Private(getResourceName(resourceInstanceIdentifier), resourceInstanceIdentifier, this))
{
    log("Starting access");
}

ResourceAccess::~ResourceAccess()
{
    log("Closing access");
    if (!d->resultHandler.isEmpty()) {
        Warning() << "Left jobs running while shutting down ResourceAccess";
    }
}

QByteArray ResourceAccess::resourceName() const
{
    return d->resourceName;
}

bool ResourceAccess::isReady() const
{
    return (d->socket && d->socket->isValid());
}

void ResourceAccess::registerCallback(uint messageId, const std::function<void(int error, const QString &errorMessage)> &callback)
{
    d->resultHandler.insert(messageId, callback);
}

KAsync::Job<void> ResourceAccess::sendCommand(int commandId)
{
    return KAsync::start<void>([this, commandId](KAsync::Future<void> &f) {
        auto continuation = [&f](int error, const QString &errorMessage) {
            if (error) {
                f.setError(error, errorMessage);
            }
            f.setFinished();
        };
        d->commandQueue << QSharedPointer<QueuedCommand>::create(commandId, continuation);
        if (isReady()) {
            processCommandQueue();
        }
    });
}

KAsync::Job<void>  ResourceAccess::sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb)
{
    //The flatbuffer is transient, but we want to store it until the job is executed
    QByteArray buffer(reinterpret_cast<const char*>(fbb.GetBufferPointer()), fbb.GetSize());
    return KAsync::start<void>([commandId, buffer, this](KAsync::Future<void> &f) {
        auto callback = [&f](int error, const QString &errorMessage) {
            if (error) {
                f.setError(error, errorMessage);
            } else {
                f.setFinished();
            }
        };

        d->commandQueue << QSharedPointer<QueuedCommand>::create(commandId, buffer, callback);
        if (isReady()) {
            processCommandQueue();
        }
    });
}

KAsync::Job<void> ResourceAccess::synchronizeResource(bool sourceSync, bool localSync)
{
    auto command = Akonadi2::CreateSynchronize(d->fbb, sourceSync, localSync);
    Akonadi2::FinishSynchronizeBuffer(d->fbb, command);
    return sendCommand(Commands::SynchronizeCommand, d->fbb);
    d->fbb.Clear();
}

void ResourceAccess::open()
{
    if (d->socket && d->socket->isValid()) {
        log("Socket valid, so not opening again");
        return;
    }
    d->initializeSocket().then<void>([this]() {
        Trace() << "Socket is initialized";
        QObject::connect(d->socket.data(), &QLocalSocket::disconnected,
                this, &ResourceAccess::disconnected);
        QObject::connect(d->socket.data(), SIGNAL(error(QLocalSocket::LocalSocketError)),
                this, SLOT(connectionError(QLocalSocket::LocalSocketError)));
        QObject::connect(d->socket.data(), &QIODevice::readyRead,
                this, &ResourceAccess::readResourceMessage);
        connected();
    }).exec();
}

void ResourceAccess::close()
{
    log(QString("Closing %1").arg(d->socket->fullServerName()));
    d->socket->close();
}

void ResourceAccess::sendCommand(const QSharedPointer<QueuedCommand> &command)
{
    Q_ASSERT(isReady());
    //TODO: we should have a timeout for commands
    d->messageId++;
    const auto messageId = d->messageId;
    log(QString("Sending command \"%1\" with messageId %2").arg(QString(Akonadi2::Commands::name(command->commandId))).arg(d->messageId));
    if (command->callback) {
        registerCallback(d->messageId, [this, messageId, command](int errorCode, QString errorMessage) {
            d->pendingCommands.remove(messageId);
            command->callback(errorCode, errorMessage);
        });
    }
    //Keep track of the command until we're sure it arrived
    d->pendingCommands.insert(d->messageId, command);
    Commands::write(d->socket.data(), d->messageId, command->commandId, command->buffer.constData(), command->buffer.size());
}

void ResourceAccess::processCommandQueue()
{
    //TODO: serialize instead of blast them all through the socket?
    Trace() << "We have " << d->commandQueue.size() << " queued commands";
    for (auto command: d->commandQueue) {
        sendCommand(command);
    }
    d->commandQueue.clear();
}

void ResourceAccess::connected()
{
    if (!isReady()) {
        return;
    }

    log(QString("Connected: %1").arg(d->socket->fullServerName()));

    {
        auto name = d->fbb.CreateString(QString::number(QCoreApplication::applicationPid()).toLatin1());
        auto command = Akonadi2::CreateHandshake(d->fbb, name);
        Akonadi2::FinishHandshakeBuffer(d->fbb, command);
        Commands::write(d->socket.data(), ++d->messageId, Commands::HandshakeCommand, d->fbb);
        d->fbb.Clear();
    }

    processCommandQueue();

    emit ready(true);
}

void ResourceAccess::disconnected()
{
    d->socket->close();
    log(QString("Disconnected from %1").arg(d->socket->fullServerName()));
    emit ready(false);
}

void ResourceAccess::connectionError(QLocalSocket::LocalSocketError error)
{
    if (error == QLocalSocket::PeerClosedError) {
        Log(d->resourceInstanceIdentifier) << "The resource closed the connection.";
    } else {
        Warning() << QString("Connection error: %1 : %2").arg(error).arg(d->socket->errorString());
    }

    //TODO We could first try to reconnect and resend the message if necessary.
    for(auto handler : d->resultHandler.values()) {
        handler(1, "The resource closed unexpectedly");
    }
    d->resultHandler.clear();
}

void ResourceAccess::readResourceMessage()
{
    if (!d->socket || !d->socket->isValid()) {
        Warning() << "No socket available";
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
        Warning() << "command too small";
        return false;
    }

    //const uint messageId = *(int*)(d->partialMessageBuffer.constData());
    const int commandId = *(int*)(d->partialMessageBuffer.constData() + sizeof(uint));
    const uint size = *(int*)(d->partialMessageBuffer.constData() + sizeof(int) + sizeof(uint));

    if (size > (uint)(d->partialMessageBuffer.size() - headerSize)) {
        Warning() << "command too small";
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
        case Commands::NotificationCommand: {
            auto buffer = GetNotification(d->partialMessageBuffer.constData() + headerSize);
            switch (buffer->type()) {
                case Akonadi2::NotificationType::NotificationType_Shutdown:
                    Log(d->resourceInstanceIdentifier) << "Received shutdown notification.";
                    close();
                    break;
                default:
                    Warning() << "Received unknown notification: " << buffer->type();
                    break;
            }
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
    Log(d->resourceInstanceIdentifier) << this << message;
}

}
