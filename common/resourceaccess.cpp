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
#include "common/createentity_generated.h"
#include "common/modifyentity_generated.h"
#include "common/deleteentity_generated.h"
#include "common/revisionreplayed_generated.h"
#include "common/inspection_generated.h"
#include "common/entitybuffer.h"
#include "common/bufferutils.h"
#include "log.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QDataStream>
#include <QBuffer>

#undef Trace
#define TracePrivate() Trace_area("client.communication." + resourceInstanceIdentifier)
#define Trace() Trace_area("client.communication." + d->resourceInstanceIdentifier)
#undef Log
#define Log() Log_area("client.communication." + d->resourceInstanceIdentifier)

static void queuedInvoke(const std::function<void()> &f, QObject *context = 0)
{
    auto timer = QSharedPointer<QTimer>::create();
    timer->setSingleShot(true);
    QObject::connect(timer.data(), &QTimer::timeout, context, [f, timer]() {
        f();
    });
    timer->start(0);
}

namespace Sink
{

struct QueuedCommand
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
    void abortPendingOperations();
    void callCallbacks();

    QByteArray resourceName;
    QByteArray resourceInstanceIdentifier;
    QSharedPointer<QLocalSocket> socket;
    QByteArray partialMessageBuffer;
    QVector<QSharedPointer<QueuedCommand>> commandQueue;
    QMap<uint, QSharedPointer<QueuedCommand>> pendingCommands;
    QMultiMap<uint, std::function<void(int error, const QString &errorMessage)> > resultHandler;
    QSet<uint> completeCommands;
    uint messageId;
    bool openingSocket;
};

ResourceAccess::Private::Private(const QByteArray &name, const QByteArray &instanceIdentifier, ResourceAccess *q)
    : resourceName(name),
      resourceInstanceIdentifier(instanceIdentifier),
      messageId(0),
      openingSocket(false)
{
}

void ResourceAccess::Private::abortPendingOperations()
{
    callCallbacks();
    if (!resultHandler.isEmpty()) {
        Warning() << "Aborting pending operations " << resultHandler.keys();
    }
    auto handlers = resultHandler.values();
    resultHandler.clear();
    for(auto handler : handlers) {
        handler(1, "The resource closed unexpectedly");
    }
}

void ResourceAccess::Private::callCallbacks()
{
    for (auto id : completeCommands) {
        //We remove the callbacks first because the handler can kill resourceaccess directly
        const auto callbacks = resultHandler.values(id);
        resultHandler.remove(id);
        for(auto handler : callbacks) {
            handler(0, QString());
        }
    }
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
    auto counter = QSharedPointer<int>::create();
    *counter = 0;
    return KAsync::dowhile([this]() -> bool {
        return !socket;
    },
    [this, counter](KAsync::Future<void> &future) {
        TracePrivate() << "Loop";
        KAsync::wait(50)
        .then(connectToServer(resourceInstanceIdentifier))
        .then<void, QSharedPointer<QLocalSocket> >([this, &future](const QSharedPointer<QLocalSocket> &s) {
            Q_ASSERT(s);
            socket = s;
            future.setFinished();
        },
        [&future, counter, this](int errorCode, const QString &errorString) {
            const int maxRetries = 10;
            if (*counter > maxRetries) {
                TracePrivate() << "Giving up";
                future.setError(-1, "Failed to connect to socket");
            } else {
                future.setFinished();
            }
            *counter = *counter + 1;
        }).exec();
    });
}

KAsync::Job<void> ResourceAccess::Private::initializeSocket()
{
    return KAsync::start<void>([this](KAsync::Future<void> &future) {
        TracePrivate() << "Trying to connect";
        connectToServer(resourceInstanceIdentifier).then<void, QSharedPointer<QLocalSocket> >([this, &future](const QSharedPointer<QLocalSocket> &s) {
            TracePrivate() << "Connected to resource, without having to start it.";
            Q_ASSERT(s);
            socket = s;
            future.setFinished();
        },
        [this, &future](int errorCode, const QString &errorString) {
            TracePrivate() << "Failed to connect, starting resource";
            //We failed to connect, so let's start the resource
            QStringList args;
            args << resourceInstanceIdentifier;
            qint64 pid = 0;
            if (QProcess::startDetached("sink_synchronizer", args, QDir::homePath(), &pid)) {
                TracePrivate() << "Started resource " << pid;
                tryToConnect()
                .then<void>([&future]() {
                    future.setFinished();
                }, [this, &future](int errorCode, const QString &errorString) {
                    Warning() << "Failed to connect to started resource";
                    future.setError(errorCode, errorString);
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

ResourceAccess::ResourceAccess(const QByteArray &resourceInstanceIdentifier)
    : ResourceAccessInterface(),
      d(new Private(getResourceName(resourceInstanceIdentifier), resourceInstanceIdentifier, this))
{
    Log() << "Starting access";
}

ResourceAccess::~ResourceAccess()
{
    Log() << "Closing access";
    if (!d->resultHandler.isEmpty()) {
        Warning() << "Left jobs running while shutting down ResourceAccess: " << d->resultHandler.keys();
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
    Trace() << "Sending synchronize command: " << sourceSync << localSync;
    flatbuffers::FlatBufferBuilder fbb;
    auto command = Sink::Commands::CreateSynchronize(fbb, sourceSync, localSync);
    Sink::Commands::FinishSynchronizeBuffer(fbb, command);
    open();
    return sendCommand(Commands::SynchronizeCommand, fbb);
}

KAsync::Job<void> ResourceAccess::sendCreateCommand(const QByteArray &resourceBufferType, const QByteArray &buffer)
{
    flatbuffers::FlatBufferBuilder fbb;
    //This is the resource buffer type and not the domain type
    auto type = fbb.CreateString(resourceBufferType.constData());
    auto delta = Sink::EntityBuffer::appendAsVector(fbb, buffer.constData(), buffer.size());
    auto location = Sink::Commands::CreateCreateEntity(fbb, 0, type, delta);
    Sink::Commands::FinishCreateEntityBuffer(fbb, location);
    open();
    return sendCommand(Sink::Commands::CreateEntityCommand, fbb);
}

KAsync::Job<void> ResourceAccess::sendModifyCommand(const QByteArray &uid, qint64 revision, const QByteArray &resourceBufferType, const QByteArrayList &deletedProperties, const QByteArray &buffer)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto entityId = fbb.CreateString(uid.constData());
    //This is the resource buffer type and not the domain type
    auto type = fbb.CreateString(resourceBufferType.constData());
    //FIXME
    auto deletions = 0;
    auto delta = Sink::EntityBuffer::appendAsVector(fbb, buffer.constData(), buffer.size());
    auto location = Sink::Commands::CreateModifyEntity(fbb, revision, entityId, deletions, type, delta);
    Sink::Commands::FinishModifyEntityBuffer(fbb, location);
    open();
    return sendCommand(Sink::Commands::ModifyEntityCommand, fbb);
}

KAsync::Job<void> ResourceAccess::sendDeleteCommand(const QByteArray &uid, qint64 revision, const QByteArray &resourceBufferType)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto entityId = fbb.CreateString(uid.constData());
    //This is the resource buffer type and not the domain type
    auto type = fbb.CreateString(resourceBufferType.constData());
    auto location = Sink::Commands::CreateDeleteEntity(fbb, revision, entityId, type);
    Sink::Commands::FinishDeleteEntityBuffer(fbb, location);
    open();
    return sendCommand(Sink::Commands::DeleteEntityCommand, fbb);
}

KAsync::Job<void> ResourceAccess::sendRevisionReplayedCommand(qint64 revision)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto location = Sink::Commands::CreateRevisionReplayed(fbb, revision);
    Sink::Commands::FinishRevisionReplayedBuffer(fbb, location);
    open();
    return sendCommand(Sink::Commands::RevisionReplayedCommand, fbb);
}

KAsync::Job<void> ResourceAccess::sendInspectionCommand(const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto id = fbb.CreateString(inspectionId.toStdString());
    auto domain = fbb.CreateString(domainType.toStdString());
    auto entity = fbb.CreateString(entityId.toStdString());
    auto prop = fbb.CreateString(property.toStdString());

    QByteArray array;
    QDataStream s(&array, QIODevice::WriteOnly);
    s << expectedValue;

    auto expected = fbb.CreateString(array.toStdString());
    auto location = Sink::Commands::CreateInspection (fbb, id, 0, entity, domain, prop, expected);
    Sink::Commands::FinishInspectionBuffer(fbb, location);
    open();
    return sendCommand(Sink::Commands::InspectionCommand, fbb);
}

void ResourceAccess::open()
{
    if (d->socket && d->socket->isValid()) {
        // Trace() << "Socket valid, so not opening again";
        return;
    }
    if (d->openingSocket) {
        return;
    }
    auto time = QSharedPointer<QTime>::create();
    time->start();
    d->openingSocket = true;
    d->initializeSocket().then<void>([this, time]() {
        Trace() << "Socket is initialized." << Log::TraceTime(time->elapsed());
        d->openingSocket = false;
        QObject::connect(d->socket.data(), &QLocalSocket::disconnected,
                this, &ResourceAccess::disconnected);
        QObject::connect(d->socket.data(), SIGNAL(error(QLocalSocket::LocalSocketError)),
                this, SLOT(connectionError(QLocalSocket::LocalSocketError)));
        QObject::connect(d->socket.data(), &QIODevice::readyRead,
                this, &ResourceAccess::readResourceMessage);
        connected();
    },
    [this](int error, const QString &errorString) {
        d->openingSocket = false;
        Warning() << "Failed to initialize socket " << errorString;
    }).exec();
}

void ResourceAccess::close()
{
    Log() << QString("Closing %1").arg(d->socket->fullServerName());
    Trace() << "Pending commands: " << d->pendingCommands.size();
    Trace() << "Queued commands: " << d->commandQueue.size();
    d->socket->close();
}

void ResourceAccess::sendCommand(const QSharedPointer<QueuedCommand> &command)
{
    Q_ASSERT(isReady());
    //TODO: we should have a timeout for commands
    d->messageId++;
    const auto messageId = d->messageId;
    Log() << QString("Sending command \"%1\" with messageId %2").arg(QString(Sink::Commands::name(command->commandId))).arg(d->messageId);
    Q_ASSERT(command->callback);
    registerCallback(d->messageId, [this, messageId, command](int errorCode, QString errorMessage) {
        Trace() << "Command complete " << messageId;
        d->pendingCommands.remove(messageId);
        command->callback(errorCode, errorMessage);
    });
    //Keep track of the command until we're sure it arrived
    d->pendingCommands.insert(d->messageId, command);
    Commands::write(d->socket.data(), d->messageId, command->commandId, command->buffer.constData(), command->buffer.size());
}

void ResourceAccess::processCommandQueue()
{
    //TODO: serialize instead of blast them all through the socket?
    Trace() << "We have " << d->commandQueue.size() << " queued commands";
    Trace() << "Pending commands: " << d->pendingCommands.size();
    for (auto command: d->commandQueue) {
        sendCommand(command);
    }
    d->commandQueue.clear();
}

void ResourceAccess::processPendingCommandQueue()
{
    Trace() << "We have " << d->pendingCommands.size() << " pending commands";
    for (auto command: d->pendingCommands) {
        Trace() << "Reenquing command " << command->commandId;
        d->commandQueue << command;
    }
    d->pendingCommands.clear();
    processCommandQueue();
}

void ResourceAccess::connected()
{
    if (!isReady()) {
        return;
    }

    Log() << QString("Connected: %1").arg(d->socket->fullServerName());

    {
        flatbuffers::FlatBufferBuilder fbb;
        auto name = fbb.CreateString(QString("PID: %1 ResourceAccess: %2").arg(QCoreApplication::applicationPid()).arg(reinterpret_cast<qlonglong>(this)).toLatin1());
        auto command = Sink::Commands::CreateHandshake(fbb, name);
        Sink::Commands::FinishHandshakeBuffer(fbb, command);
        Commands::write(d->socket.data(), ++d->messageId, Commands::HandshakeCommand, fbb);
    }

    //Reenqueue pending commands, we failed to send them
    processPendingCommandQueue();
    //Send queued commands
    processCommandQueue();

    emit ready(true);
}

void ResourceAccess::disconnected()
{
    Log() << QString("Disconnected from %1").arg(d->socket->fullServerName());
    d->socket->close();
    emit ready(false);
}

void ResourceAccess::connectionError(QLocalSocket::LocalSocketError error)
{
    if (error == QLocalSocket::PeerClosedError) {
        Log() << "The resource closed the connection.";
        d->abortPendingOperations();
    } else {
        Warning() << QString("Connection error: %1 : %2").arg(error).arg(d->socket->errorString());
        if (d->pendingCommands.size()) {
            Trace() << "Reconnecting due to pending operations: " << d->pendingCommands.size();
            open();
        }
    }
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
            auto buffer = Commands::GetRevisionUpdate(d->partialMessageBuffer.constData() + headerSize);
            Log() << QString("Revision updated to: %1").arg(buffer->revision());
            Notification n;
            n.type = Sink::Commands::NotificationType::NotificationType_RevisionUpdate;
            emit notification(n);
            emit revisionChanged(buffer->revision());

            break;
        }
        case Commands::CommandCompletionCommand: {
            auto buffer = Commands::GetCommandCompletion(d->partialMessageBuffer.constData() + headerSize);
            Log() << QString("Command with messageId %1 completed %2").arg(buffer->id()).arg(buffer->success() ? "sucessfully" : "unsuccessfully");

            d->completeCommands << buffer->id();
            //The callbacks can result in this object getting destroyed directly, so we need to ensure we finish our work first
            queuedInvoke([=]() {
                d->callCallbacks();
            }, this);
            break;
        }
        case Commands::NotificationCommand: {
            auto buffer = Commands::GetNotification(d->partialMessageBuffer.constData() + headerSize);
            switch (buffer->type()) {
                case Sink::Commands::NotificationType::NotificationType_Shutdown:
                    Log() << "Received shutdown notification.";
                    close();
                    break;
                case Sink::Commands::NotificationType::NotificationType_Inspection: {
                    Log() << "Received inspection notification.";
                    Notification n;
                    if (buffer->identifier()) {
                        //Don't use fromRawData, the buffer is gone once we invoke emit notification
                        n.id = BufferUtils::extractBufferCopy(buffer->identifier());
                    }
                    if (buffer->message()) {
                        //Don't use fromRawData, the buffer is gone once we invoke emit notification
                        n.message = BufferUtils::extractBufferCopy(buffer->message());
                    }
                    n.type = buffer->type();
                    n.code = buffer->code();
                    //The callbacks can result in this object getting destroyed directly, so we need to ensure we finish our work first
                    queuedInvoke([=]() {
                        emit notification(n);
                    }, this);
                }
                    break;
                case Sink::Commands::NotificationType::NotificationType_Status:
                case Sink::Commands::NotificationType::NotificationType_Warning:
                case Sink::Commands::NotificationType::NotificationType_Progress:
                case Sink::Commands::NotificationType::NotificationType_RevisionUpdate:
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

}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "moc_resourceaccess.cpp"
#pragma clang diagnostic pop
