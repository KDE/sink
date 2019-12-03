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
#include "common/flush_generated.h"
#include "common/secret_generated.h"
#include "common/entitybuffer.h"
#include "common/bufferutils.h"
#include "common/test.h"
#include "common/secretstore.h"
#include "log.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QDataStream>
#include <QBuffer>
#include <QTime>
#include <QStandardPaths>

static void queuedInvoke(const std::function<void()> &f, QObject *context = nullptr)
{
    auto timer = QSharedPointer<QTimer>::create();
    timer->setSingleShot(true);
    QObject::connect(timer.data(), &QTimer::timeout, context, [f, timer]() { f(); });
    timer->start(0);
}

namespace Sink {

struct QueuedCommand
{
public:
    QueuedCommand(int commandId, const std::function<void(int, const QString &)> &callback) : commandId(commandId), callback(callback)
    {
    }

    QueuedCommand(int commandId, const QByteArray &b, const std::function<void(int, const QString &)> &callback) : commandId(commandId), buffer(b), callback(callback)
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
    ~Private();
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
    QMultiMap<uint, std::function<void(int error, const QString &errorMessage)>> resultHandler;
    QHash<uint, bool> completeCommands;
    uint messageId;
    bool openingSocket;
    SINK_DEBUG_COMPONENT(resourceInstanceIdentifier)
};


ResourceAccess::Private::Private(const QByteArray &name, const QByteArray &instanceIdentifier, ResourceAccess *q)
    : resourceName(name), resourceInstanceIdentifier(instanceIdentifier), messageId(0), openingSocket(false)
{
}
ResourceAccess::Private::~Private()
{
}

void ResourceAccess::Private::abortPendingOperations()
{
    callCallbacks();
    if (!resultHandler.isEmpty()) {
        SinkWarning() << "Aborting pending operations " << resultHandler.keys();
    }
    auto handlers = resultHandler.values();
    resultHandler.clear();
    for (auto handler : handlers) {
        handler(1, "The resource closed unexpectedly");
    }
    for (auto queuedCommand : commandQueue) {
        queuedCommand->callback(1, "The resource closed unexpectedly");
    }
    commandQueue.clear();
}

void ResourceAccess::Private::callCallbacks()
{
    const auto commandIds = completeCommands.keys();
    for (auto id : commandIds) {
        const bool success = completeCommands.take(id);
        // We remove the callbacks first because the handler can kill resourceaccess directly
        const auto callbacks = resultHandler.values(id);
        resultHandler.remove(id);
        for (auto handler : callbacks) {
            if (success) {
                handler(0, QString());
            } else {
                handler(1, "Command failed.");
            }
        }
    }
}

// Connects to server and returns connected socket on success
KAsync::Job<QSharedPointer<QLocalSocket>> ResourceAccess::connectToServer(const QByteArray &identifier)
{
    auto s = QSharedPointer<QLocalSocket>{new QLocalSocket, &QObject::deleteLater};
    return KAsync::start<QSharedPointer<QLocalSocket>>([identifier, s](KAsync::Future<QSharedPointer<QLocalSocket>> &future) {
        SinkTrace() << "Connecting to server " << identifier;
        auto context = new QObject;
        QObject::connect(s.data(), &QLocalSocket::connected, context, [&future, &s, context, identifier]() {
            SinkTrace() << "Connected to server " << identifier;
            Q_ASSERT(s);
            delete context;
            future.setValue(s);
            future.setFinished();
        });
        QObject::connect(s.data(), static_cast<void (QLocalSocket::*)(QLocalSocket::LocalSocketError)>(&QLocalSocket::error), context, [&future, &s, context, identifier](QLocalSocket::LocalSocketError localSocketError) {
            SinkTrace() << "Failed to connect to server " << identifier;
            const auto errorString = s->errorString();
            const auto name = s->fullServerName();
            delete context;
            future.setError(localSocketError, QString("Failed to connect to socket %1: %2").arg(name).arg(errorString));
            future.setError({1, QString("Failed to connect to socket %1: %2 %3").arg(name).arg(localSocketError).arg(errorString)});
        });
        s->connectToServer(identifier);
    });
}

KAsync::Job<void> ResourceAccess::Private::tryToConnect()
{
    // We may have a socket from the last connection leftover
    socket.reset();
    auto counter = QSharedPointer<int>::create(0);
    return KAsync::doWhile(
        [this, counter]() {
            SinkTrace() << "Try to connect " << resourceInstanceIdentifier;
            return connectToServer(resourceInstanceIdentifier)
                .then<KAsync::ControlFlowFlag, QSharedPointer<QLocalSocket>>(
                    [this, counter](const KAsync::Error &error, const QSharedPointer<QLocalSocket> &s) {
                        if (error) {
                            static int waitTime = 10;
                            static int timeout = 20000;
                            static int maxRetries = timeout / waitTime;
                            if (*counter >= maxRetries) {
                                SinkTrace() << "Giving up after " << *counter << "tries";
                                return KAsync::error<KAsync::ControlFlowFlag>(error);
                            } else {
                                *counter = *counter + 1;
                                return KAsync::wait(waitTime).then(KAsync::value(KAsync::Continue));
                            }
                        } else {
                            Q_ASSERT(s);
                            socket = s;
                            return KAsync::value(KAsync::Break);
                        }
                    });
        });
}

KAsync::Job<void> ResourceAccess::Private::initializeSocket()
{
    return KAsync::start<void>([this] {
        SinkTrace() << "Trying to connect";
        return connectToServer(resourceInstanceIdentifier)
            .then<void, QSharedPointer<QLocalSocket>>(
                [this](const KAsync::Error &error, const QSharedPointer<QLocalSocket> &s) {
                    if (error) {
                        // We failed to connect, so let's start the resource
                        QStringList args;
                        if (Sink::Test::testModeEnabled()) {
                            args << "--test";
                        }
                        if (resourceName.isEmpty()) {
                            SinkWarning() << "No resource type given";
                            return KAsync::error();
                        }
                        args << resourceInstanceIdentifier << resourceName;

                        //Prefer a binary next to this binary, otherwise fall-back to PATH. Necessary for MacOS bundles because the bundle is not in the PATH.
                        auto executable = QStandardPaths::findExecutable("sink_synchronizer", {QCoreApplication::applicationDirPath()});
                        if (executable.isEmpty()) {
                            executable = QStandardPaths::findExecutable("sink_synchronizer");
                        }
                        if (executable.isEmpty()) {
                            SinkError() << "Failed to find the sink_synchronizer binary in the paths: " << QCoreApplication::applicationDirPath();
                            return KAsync::error("Failed to find the sink_synchronizer binary.");
                        }
                        qint64 pid = 0;
                        SinkLog() << "Starting resource " << executable << args.join(" ") << "Home path: " << QDir::homePath();
                        if (QProcess::startDetached(executable, args, QDir::homePath(), &pid)) {
                            SinkTrace() << "Started resource " << resourceInstanceIdentifier << pid;
                            return tryToConnect()
                                .onError([this, args](const KAsync::Error &error) {
                                    SinkError() << "Failed to connect to started resource: sink_synchronizer " << args;
                                });
                        } else {
                            SinkError() << "Failed to start resource " << resourceInstanceIdentifier;
                            return KAsync::error("Failed to start resource.");
                        }
                    } else {
                        SinkTrace() << "Connected to resource, without having to start it.";
                        Q_ASSERT(s);
                        socket = s;
                        return KAsync::null();
                    }
                });
    });
}

ResourceAccess::ResourceAccess(const QByteArray &resourceInstanceIdentifier, const QByteArray &resourceType)
    : ResourceAccessInterface(), d(new Private(resourceType, resourceInstanceIdentifier, this))
{
    mResourceStatus = Sink::ApplicationDomain::NoStatus;
    SinkTrace() << "Starting access";

    QObject::connect(&SecretStore::instance(), &SecretStore::secretAvailable, this, [this] (const QByteArray &resourceId) {
        if (resourceId == d->resourceInstanceIdentifier) {
            sendSecret(SecretStore::instance().resourceSecret(d->resourceInstanceIdentifier)).exec();
        }
    });
}

ResourceAccess::~ResourceAccess()
{
    SinkLog() << "Closing access";
    if (!d->resultHandler.isEmpty()) {
        SinkWarning() << "Left jobs running while shutting down ResourceAccess: " << d->resultHandler.keys();
    }
    delete d;
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

void ResourceAccess::enqueueCommand(const QSharedPointer<QueuedCommand> &command)
{
    d->commandQueue << command;
    if (isReady()) {
        processCommandQueue();
    } else {
        open();
    }
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
        enqueueCommand(QSharedPointer<QueuedCommand>::create(commandId, continuation));
    });
}

KAsync::Job<void> ResourceAccess::sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb)
{
    // The flatbuffer is transient, but we want to store it until the job is executed
    QByteArray buffer(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
    return KAsync::start<void>([commandId, buffer, this](KAsync::Future<void> &f) {
        auto callback = [&f](int error, const QString &errorMessage) {
            if (error) {
                f.setError(error, errorMessage);
            } else {
                f.setFinished();
            }
        };
        enqueueCommand(QSharedPointer<QueuedCommand>::create(commandId, buffer, callback));
    });
}

KAsync::Job<void> ResourceAccess::synchronizeResource(const Sink::QueryBase &query)
{
    flatbuffers::FlatBufferBuilder fbb;
    QByteArray queryString;
    {
        QDataStream stream(&queryString, QIODevice::WriteOnly);
        stream << query;
    }
    auto q = fbb.CreateString(queryString.toStdString());
    auto builder = Sink::Commands::SynchronizeBuilder(fbb);
    builder.add_query(q);
    Sink::Commands::FinishSynchronizeBuffer(fbb, builder.Finish());

    return sendCommand(Commands::SynchronizeCommand, fbb);
}

KAsync::Job<void> ResourceAccess::sendCreateCommand(const QByteArray &uid, const QByteArray &resourceBufferType, const QByteArray &buffer)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto entityId = fbb.CreateString(uid.constData());
    // This is the resource buffer type and not the domain type
    auto type = fbb.CreateString(resourceBufferType.constData());
    auto delta = Sink::EntityBuffer::appendAsVector(fbb, buffer.constData(), buffer.size());
    auto location = Sink::Commands::CreateCreateEntity(fbb, entityId, type, delta);
    Sink::Commands::FinishCreateEntityBuffer(fbb, location);
    return sendCommand(Sink::Commands::CreateEntityCommand, fbb);
}

KAsync::Job<void>
ResourceAccess::sendModifyCommand(const QByteArray &uid, qint64 revision, const QByteArray &resourceBufferType, const QByteArrayList &deletedProperties, const QByteArray &buffer, const QByteArrayList &changedProperties, const QByteArray &newResource, bool remove)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto entityId = fbb.CreateString(uid.constData());
    auto type = fbb.CreateString(resourceBufferType.constData());
    auto modifiedProperties = BufferUtils::toVector(fbb, changedProperties);
    auto deletions = BufferUtils::toVector(fbb, deletedProperties);
    auto delta = Sink::EntityBuffer::appendAsVector(fbb, buffer.constData(), buffer.size());
    auto resource = newResource.isEmpty() ? 0 : fbb.CreateString(newResource.constData());
    auto location = Sink::Commands::CreateModifyEntity(fbb, revision, entityId, deletions, type, delta, true, modifiedProperties, resource, remove);
    Sink::Commands::FinishModifyEntityBuffer(fbb, location);
    return sendCommand(Sink::Commands::ModifyEntityCommand, fbb);
}

KAsync::Job<void> ResourceAccess::sendDeleteCommand(const QByteArray &uid, qint64 revision, const QByteArray &resourceBufferType)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto entityId = fbb.CreateString(uid.constData());
    auto type = fbb.CreateString(resourceBufferType.constData());
    auto location = Sink::Commands::CreateDeleteEntity(fbb, revision, entityId, type);
    Sink::Commands::FinishDeleteEntityBuffer(fbb, location);
    return sendCommand(Sink::Commands::DeleteEntityCommand, fbb);
}

KAsync::Job<void> ResourceAccess::sendRevisionReplayedCommand(qint64 revision)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto location = Sink::Commands::CreateRevisionReplayed(fbb, revision);
    Sink::Commands::FinishRevisionReplayedBuffer(fbb, location);
    return sendCommand(Sink::Commands::RevisionReplayedCommand, fbb);
}

KAsync::Job<void>
ResourceAccess::sendInspectionCommand(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
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
    auto location = Sink::Commands::CreateInspection(fbb, id, inspectionType, entity, domain, prop, expected);
    Sink::Commands::FinishInspectionBuffer(fbb, location);
    return sendCommand(Sink::Commands::InspectionCommand, fbb);
}

KAsync::Job<void> ResourceAccess::sendFlushCommand(int flushType, const QByteArray &flushId)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto id = fbb.CreateString(flushId.toStdString());
    auto location = Sink::Commands::CreateFlush(fbb, id, flushType);
    Sink::Commands::FinishFlushBuffer(fbb, location);
    return sendCommand(Sink::Commands::FlushCommand, fbb);
}

KAsync::Job<void> ResourceAccess::sendSecret(const QString &secret)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto s = fbb.CreateString(secret.toStdString());
    auto location = Sink::Commands::CreateSecret(fbb, s);
    Sink::Commands::FinishSecretBuffer(fbb, location);
    return sendCommand(Sink::Commands::SecretCommand, fbb);
}

KAsync::Job<void> ResourceAccess::shutdown()
{
    return sendCommand(Sink::Commands::ShutdownCommand);
}

void ResourceAccess::open()
{
    if (d->socket && d->socket->isValid()) {
        // SinkTrace() << "Socket valid, so not opening again";
        return;
    }
    if (d->openingSocket) {
        return;
    }
    auto time = QSharedPointer<QTime>::create();
    time->start();
    d->openingSocket = true;
    d->initializeSocket()
        .then<void>(
            [this, time](const KAsync::Error &error) {
                d->openingSocket = false;
                if (error) {
                    SinkError() << "Failed to initialize socket " << error;
                    d->abortPendingOperations();
                } else {
                    SinkTrace() << "Socket is initialized." << Log::TraceTime(time->elapsed());
                    Q_ASSERT(d->socket);
                    QObject::connect(d->socket.data(), &QLocalSocket::disconnected, this, &ResourceAccess::disconnected);
                    QObject::connect(d->socket.data(), SIGNAL(error(QLocalSocket::LocalSocketError)), this, SLOT(connectionError(QLocalSocket::LocalSocketError)));
                    QObject::connect(d->socket.data(), &QIODevice::readyRead, this, &ResourceAccess::readResourceMessage);
                    connected();
                }
                return KAsync::null();
            })
        .exec();
}

void ResourceAccess::close()
{
    SinkLog() << QString("Closing %1").arg(d->socket->fullServerName());
    SinkTrace() << "Pending commands: " << d->pendingCommands.size();
    SinkTrace() << "Queued commands: " << d->commandQueue.size();
    d->socket->close();
}

void ResourceAccess::sendCommand(const QSharedPointer<QueuedCommand> &command)
{
    Q_ASSERT(isReady());
    // TODO: we should have a timeout for commands
    d->messageId++;
    const auto messageId = d->messageId;
    SinkTrace() << QString("Sending command \"%1\" with messageId %2").arg(QString(Sink::Commands::name(command->commandId))).arg(d->messageId);
    Q_ASSERT(command->callback);
    registerCallback(d->messageId, [this, messageId, command](int errorCode, QString errorMessage) {
        SinkTrace() << "Command complete " << messageId;
        d->pendingCommands.remove(messageId);
        command->callback(errorCode, errorMessage);
    });
    // Keep track of the command until we're sure it arrived
    d->pendingCommands.insert(d->messageId, command);
    Commands::write(d->socket.data(), d->messageId, command->commandId, command->buffer.constData(), command->buffer.size());
}

void ResourceAccess::processCommandQueue()
{
    // TODO: serialize instead of blast them all through the socket?
    SinkTrace() << "We have " << d->commandQueue.size() << " queued commands";
    SinkTrace() << "Pending commands: " << d->pendingCommands.size();
    for (auto command : d->commandQueue) {
        sendCommand(command);
    }
    d->commandQueue.clear();
}

void ResourceAccess::processPendingCommandQueue()
{
    SinkTrace() << "We have " << d->pendingCommands.size() << " pending commands";
    for (auto command : d->pendingCommands) {
        SinkTrace() << "Reenquing command " << command->commandId;
        d->commandQueue << command;
    }
    d->pendingCommands.clear();
    processCommandQueue();
}

void ResourceAccess::connected()
{
    if (!isReady()) {
        SinkTrace() << "Connected but not ready?";
        return;
    }

    SinkTrace() << QString("Connected: %1").arg(d->socket->fullServerName());

    {
        flatbuffers::FlatBufferBuilder fbb;
        auto name = fbb.CreateString(QString("PID: %1 ResourceAccess: %2").arg(QCoreApplication::applicationPid()).arg(reinterpret_cast<qlonglong>(this)).toLatin1().toStdString());
        auto command = Sink::Commands::CreateHandshake(fbb, name);
        Sink::Commands::FinishHandshakeBuffer(fbb, command);
        Commands::write(d->socket.data(), ++d->messageId, Commands::HandshakeCommand, fbb);
    }

    // Reenqueue pending commands, we failed to send them
    processPendingCommandQueue();
    auto secret = SecretStore::instance().resourceSecret(d->resourceInstanceIdentifier);
    if (!secret.isEmpty()) {
        sendSecret(secret).exec();
    }

    emit ready(true);
}

void ResourceAccess::disconnected()
{
    SinkLog() << QString("Disconnected from %1").arg(d->socket->fullServerName());
    //Ensure we read all remaining data before closing the socket.
    //This is required on windows at least.
    readResourceMessage();
    d->socket->close();
    emit ready(false);
}

void ResourceAccess::connectionError(QLocalSocket::LocalSocketError error)
{
    const bool resourceCrashed = d->partialMessageBuffer.contains("PANIC");
    if (resourceCrashed) {
        SinkError() << "The resource crashed!";
        mResourceStatus = Sink::ApplicationDomain::ErrorStatus;
        Sink::Notification n;
        n.type = Sink::Notification::Status;
        emit notification(n);
        Sink::Notification crashNotification;
        crashNotification.type = Sink::Notification::Error;
        crashNotification.code = Sink::ApplicationDomain::ResourceCrashedError;
        emit notification(crashNotification);
        d->abortPendingOperations();
    } else if (error == QLocalSocket::PeerClosedError) {
        SinkLog() << "The resource closed the connection.";
        d->abortPendingOperations();
    } else {
        SinkWarning() << QString("Connection error: %1 : %2").arg(error).arg(d->socket->errorString());
        if (d->pendingCommands.size()) {
            SinkTrace() << "Reconnecting due to pending operations: " << d->pendingCommands.size();
            open();
        }
    }
}

void ResourceAccess::readResourceMessage()
{
    if (!d->socket) {
        SinkWarning() << "No socket available";
        return;
    }

    if (d->socket->bytesAvailable()) {
        d->partialMessageBuffer += d->socket->readAll();

        // should be scheduled rather than processed all at once
        while (processMessageBuffer()) {
        }
    }
}

static Sink::Notification getNotification(const Sink::Commands::Notification *buffer)
{
    Sink::Notification n;
    if (buffer->identifier()) {
        // Don't use fromRawData, the buffer is gone once we invoke emit notification
        n.id = BufferUtils::extractBufferCopy(buffer->identifier());
    }
    if (buffer->message()) {
        // Don't use fromRawData, the buffer is gone once we invoke emit notification
        n.message = BufferUtils::extractBufferCopy(buffer->message());
    }
    n.type = buffer->type();
    n.code = buffer->code();
    n.progress = buffer->progress();
    n.total = buffer->total();
    n.entities = BufferUtils::fromVector(*buffer->entities());
    return n;
}

bool ResourceAccess::processMessageBuffer()
{
    static const int headerSize = Commands::headerSize();
    if (d->partialMessageBuffer.size() < headerSize) {
        //This is not an error
        SinkTrace() << "command too small, smaller than headerSize: " << d->partialMessageBuffer.size() << headerSize;
        return false;
    }

    // const uint messageId = *(int*)(d->partialMessageBuffer.constData());
    const int commandId = *(const int *)(d->partialMessageBuffer.constData() + sizeof(uint));
    const uint size = *(const int *)(d->partialMessageBuffer.constData() + sizeof(int) + sizeof(uint));

    const uint availableMessageSize = d->partialMessageBuffer.size() - headerSize;
    if (size > availableMessageSize) {
        //This is not an error
        SinkTrace() << "command too small, message smaller than advertised: " << availableMessageSize << headerSize;
        return false;
    }

    switch (commandId) {
        case Commands::RevisionUpdateCommand: {
            auto buffer = Commands::GetRevisionUpdate(d->partialMessageBuffer.constData() + headerSize);
            SinkTrace() << QString("Revision updated to: %1").arg(buffer->revision());
            Notification n;
            n.type = Sink::Notification::RevisionUpdate;
            emit notification(n);
            emit revisionChanged(buffer->revision());

            break;
        }
        case Commands::CommandCompletionCommand: {
            auto buffer = Commands::GetCommandCompletion(d->partialMessageBuffer.constData() + headerSize);
            SinkTrace() << QString("Command with messageId %1 completed %2").arg(buffer->id()).arg(buffer->success() ? "sucessfully" : "unsuccessfully");

            d->completeCommands.insert(buffer->id(), buffer->success());
            // The callbacks can result in this object getting destroyed directly, so we need to ensure we finish our work first
            queuedInvoke([=]() { d->callCallbacks(); }, this);
            break;
        }
        case Commands::NotificationCommand: {
            auto buffer = Commands::GetNotification(d->partialMessageBuffer.constData() + headerSize);
            switch (buffer->type()) {
                case Sink::Notification::Shutdown:
                    SinkLog() << "Received shutdown notification.";
                    close();
                    break;
                case Sink::Notification::Inspection: {
                    SinkTrace() << "Received inspection notification.";
                    auto n = getNotification(buffer);
                    // The callbacks can result in this object getting destroyed directly, so we need to ensure we finish our work first
                    queuedInvoke([=]() { emit notification(n); }, this);
                } break;
                case Sink::Notification::Status:
                    if (mResourceStatus != buffer->code()) {
                        mResourceStatus = buffer->code();
                        SinkTrace() << "Updated status: " << mResourceStatus;
                    }
                    [[clang::fallthrough]];
                case Sink::Notification::Info:
                    [[clang::fallthrough]];
                case Sink::Notification::Warning:
                    [[clang::fallthrough]];
                case Sink::Notification::Error:
                    [[clang::fallthrough]];
                case Sink::Notification::FlushCompletion:
                    [[clang::fallthrough]];
                case Sink::Notification::Progress: {
                    auto n = getNotification(buffer);
                    SinkTrace() << "Received notification: " << n;
                    n.resource = d->resourceInstanceIdentifier;
                    emit notification(n);
                } break;
                case Sink::Notification::RevisionUpdate:
                default:
                    SinkWarning() << "Received unknown notification: " << buffer->type();
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



ResourceAccessFactory::ResourceAccessFactory()
{

}

ResourceAccessFactory &ResourceAccessFactory::instance()
{
    static ResourceAccessFactory *instance = nullptr;
    if (!instance) {
        instance = new ResourceAccessFactory;
    }
    return *instance;
}

Sink::ResourceAccess::Ptr ResourceAccessFactory::getAccess(const QByteArray &instanceIdentifier, const QByteArray resourceType)
{
    if (!mCache.contains(instanceIdentifier)) {
        // Reuse the pointer if something else kept the resourceaccess alive
        if (mWeakCache.contains(instanceIdentifier)) {
            if (auto sharedPointer = mWeakCache.value(instanceIdentifier).toStrongRef()) {
                mCache.insert(instanceIdentifier, sharedPointer);
            }
        }
        if (!mCache.contains(instanceIdentifier)) {
            // Create a new instance if necessary
            auto sharedPointer = Sink::ResourceAccess::Ptr{new Sink::ResourceAccess(instanceIdentifier, resourceType), &QObject::deleteLater};
            QObject::connect(sharedPointer.data(), &Sink::ResourceAccess::ready, sharedPointer.data(), [this, instanceIdentifier](bool ready) {
                if (!ready) {
                    //We want to remove, but we don't want shared pointer to be destroyed until end of the function as this might trigger further steps.
                    auto ptr = mCache.take(instanceIdentifier);
                    if (auto timer = mTimer.take(instanceIdentifier)) {
                        timer->stop();
                    }
                    Q_UNUSED(ptr);
                }
            });
            mCache.insert(instanceIdentifier, sharedPointer);
            mWeakCache.insert(instanceIdentifier, sharedPointer);
        }
    }
    if (!mTimer.contains(instanceIdentifier)) {
        auto timer = QSharedPointer<QTimer>::create();
        timer->setSingleShot(true);
        // Drop connection after 3 seconds (which is a random value)
        QObject::connect(timer.data(), &QTimer::timeout, timer.data(), [this, instanceIdentifier]() {
            //We want to remove, but we don't want shared pointer to be destroyed until end of the function as this might trigger further steps.
            auto ptr = mCache.take(instanceIdentifier);
            Q_UNUSED(ptr);
        });
        timer->setInterval(3000);
        mTimer.insert(instanceIdentifier, timer);
    }
    mTimer.value(instanceIdentifier)->start();
    return mCache.value(instanceIdentifier);
}
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "moc_resourceaccess.cpp"
#pragma clang diagnostic pop
