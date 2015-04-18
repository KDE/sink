/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include "listener.h"

#include "common/clientapi.h"
#include "common/console.h"
#include "common/commands.h"
#include "common/resource.h"
#include "common/log.h"

// commands
#include "common/commandcompletion_generated.h"
#include "common/handshake_generated.h"
#include "common/revisionupdate_generated.h"
#include "common/synchronize_generated.h"
#include "common/notification_generated.h"

#include <QLocalSocket>
#include <QTimer>

Listener::Listener(const QByteArray &resourceName, QObject *parent)
    : QObject(parent),
      m_server(new QLocalServer(this)),
      m_resourceName(resourceName),
      m_resource(0),
      m_pipeline(new Akonadi2::Pipeline(resourceName, parent)),
      m_clientBufferProcessesTimer(new QTimer(this)),
      m_messageId(0)
{
    connect(m_pipeline, &Akonadi2::Pipeline::revisionUpdated,
            this, &Listener::refreshRevision);
    connect(m_server, &QLocalServer::newConnection,
             this, &Listener::acceptConnection);
    Trace() << "Trying to open " << m_resourceName;
    if (!m_server->listen(QString::fromLatin1(resourceName))) {
        // FIXME: multiple starts need to be handled here
        m_server->removeServer(resourceName);
        if (!m_server->listen(QString::fromLatin1(resourceName))) {
            Warning() << "Utter failure to start server";
            exit(-1);
        }
    }

    if (m_server->isListening()) {
        Log() << QString("Listening on %1").arg(m_server->serverName());
    }

    m_checkConnectionsTimer = new QTimer;
    m_checkConnectionsTimer->setSingleShot(true);
    m_checkConnectionsTimer->setInterval(1000);
    connect(m_checkConnectionsTimer, &QTimer::timeout, [this]() {
        if (m_connections.isEmpty()) {
            Log() << QString("No connections, shutting down.");
            quit();
        }
    });

    //TODO: experiment with different timeouts
    //      or even just drop down to invoking the method queued? => invoke queued unless we need throttling
    m_clientBufferProcessesTimer->setInterval(0);
    m_clientBufferProcessesTimer->setSingleShot(true);
    connect(m_clientBufferProcessesTimer, &QTimer::timeout,
            this, &Listener::processClientBuffers);
}

Listener::~Listener()
{
}

void Listener::closeAllConnections()
{
    for (Client &client: m_connections) {
        if (client.socket) {
            client.socket->close();
            delete client.socket;
            client.socket = 0;
        }
    }

    m_connections.clear();
}

void Listener::acceptConnection()
{
    Trace() << "Accepting connection";
    QLocalSocket *socket = m_server->nextPendingConnection();

    if (!socket) {
        return;
    }

    Log() << "Got a connection";
    Client client("Unknown Client", socket);
    connect(socket, &QIODevice::readyRead,
            this, &Listener::readFromSocket);
    m_connections << client;
    connect(socket, &QLocalSocket::disconnected,
            this, &Listener::clientDropped);
    m_checkConnectionsTimer->stop();

}

void Listener::clientDropped()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }

    bool dropped = false;
    QMutableVectorIterator<Client> it(m_connections);
    while (it.hasNext()) {
        const Client &client = it.next();
        if (client.socket == socket) {
            dropped = true;
            Log() << QString("Dropped connection: %1").arg(client.name) << socket;
            it.remove();
            break;
        }
    }
    if (!dropped) {
        Warning() << "Failed to find connection for disconnected socket: " << socket;
    }

    checkConnections();
}

void Listener::checkConnections()
{
    m_checkConnectionsTimer->start();
}

void Listener::readFromSocket()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }

    Trace() << "Reading from socket...";
    for (Client &client: m_connections) {
        if (client.socket == socket) {
            client.commandBuffer += socket->readAll();
            if (processClientBuffer(client) && !m_clientBufferProcessesTimer->isActive()) {
                // we have more client buffers to handle
                m_clientBufferProcessesTimer->start();
            }
            break;
        }
    }
}

void Listener::processClientBuffers()
{
    //TODO: we should not process all clients, but iterate async over them and process
    //      one command from each in turn to ensure all clients get fair handling of
    //      commands?
    bool again = false;
    for (Client &client: m_connections) {
        if (!client.socket || !client.socket->isValid() || client.commandBuffer.isEmpty()) {
            continue;
        }

        if (processClientBuffer(client)) {
            again = true;
        }
    }

    if (again) {
        m_clientBufferProcessesTimer->start();
    }
}

void Listener::processCommand(int commandId, uint messageId, Client &client, uint size, const std::function<void()> &callback)
{
    switch (commandId) {
        case Akonadi2::Commands::HandshakeCommand: {
            flatbuffers::Verifier verifier((const uint8_t *)client.commandBuffer.constData(), size);
            if (Akonadi2::VerifyHandshakeBuffer(verifier)) {
                auto buffer = Akonadi2::GetHandshake(client.commandBuffer.constData());
                client.name = buffer->name()->c_str();
                sendCurrentRevision(client);
            } else {
                Warning() << "received invalid command";
            }
            break;
        }
        case Akonadi2::Commands::SynchronizeCommand: {
            flatbuffers::Verifier verifier((const uint8_t *)client.commandBuffer.constData(), size);
            if (Akonadi2::VerifySynchronizeBuffer(verifier)) {
                auto buffer = Akonadi2::GetSynchronize(client.commandBuffer.constData());
                Log() << QString("\tSynchronize request (id %1) from %2").arg(messageId).arg(client.name);
                loadResource();
                if (!m_resource) {
                    Warning() << "No resource loaded";
                    break;
                }
                auto job = Async::null<void>();
                if (buffer->sourceSync()) {
                    job = m_resource->synchronizeWithSource(m_pipeline);
                }
                if (buffer->localSync()) {
                    job = job.then<void>(m_resource->processAllMessages());
                }
                job.then<void>([callback]() {
                    callback();
                }).exec();
                return;
            } else {
                Warning() << "received invalid command";
            }
            break;
        }
        case Akonadi2::Commands::FetchEntityCommand:
        case Akonadi2::Commands::DeleteEntityCommand:
        case Akonadi2::Commands::ModifyEntityCommand:
        case Akonadi2::Commands::CreateEntityCommand:
            Log() << "\tCommand id  " << messageId << " of type \"" << Akonadi2::Commands::name(commandId) << "\" from " << client.name;
            loadResource();
            if (m_resource) {
                m_resource->processCommand(commandId, client.commandBuffer, size, m_pipeline);
            }
            break;
        case Akonadi2::Commands::ShutdownCommand:
            Log() << QString("\tReceived shutdown command from %1").arg(client.name);
            QTimer::singleShot(0, this, &Listener::quit);
            break;
        default:
            if (commandId > Akonadi2::Commands::CustomCommand) {
                Log() << QString("\tReceived custom command from %1: ").arg(client.name) << commandId;
                loadResource();
                if (m_resource) {
                    m_resource->processCommand(commandId, client.commandBuffer, size, m_pipeline);
                }
            } else {
                Warning() << QString("\tReceived invalid command from %1: ").arg(client.name) << commandId;
                //TODO: handle error: we don't know wtf this command is
            }
            break;
    }
    callback();
}

void Listener::quit()
{
    //Broadcast shutdown notifications to open clients, so they don't try to restart the resource
    auto command = Akonadi2::CreateNotification(m_fbb, Akonadi2::NotificationType::NotificationType_Shutdown);
    Akonadi2::FinishNotificationBuffer(m_fbb, command);
    for (Client &client : m_connections) {
        if (client.socket && client.socket->isOpen()) {
            Akonadi2::Commands::write(client.socket, ++m_messageId, Akonadi2::Commands::NotificationCommand, m_fbb);
        }
    }
    m_fbb.Clear();

    m_server->close();
    emit noClients();
}

bool Listener::processClientBuffer(Client &client)
{
    static const int headerSize = Akonadi2::Commands::headerSize();
    if (client.commandBuffer.size() < headerSize) {
        return false;
    }

    const uint messageId = *(uint*)client.commandBuffer.constData();
    const int commandId = *(int*)(client.commandBuffer.constData() + sizeof(uint));
    const uint size = *(uint*)(client.commandBuffer.constData() + sizeof(int) + sizeof(uint));

    //TODO: reject messages above a certain size?

    if (size <= uint(client.commandBuffer.size() - headerSize)) {
        client.commandBuffer.remove(0, headerSize);

        auto socket = QPointer<QLocalSocket>(client.socket);
        auto clientName = client.name;
        processCommand(commandId, messageId, client, size, [this, messageId, commandId, socket, clientName]() {
            Log() << QString("\tCompleted command messageid %1 of type \"%2\" from %3").arg(messageId).arg(QString(Akonadi2::Commands::name(commandId))).arg(clientName);
            if (socket) {
                sendCommandCompleted(socket.data(), messageId);
            } else {
                Log() << QString("Socket became invalid before we could send a response. client: %1").arg(clientName);
            }
        });
        client.commandBuffer.remove(0, size);

        return client.commandBuffer.size() >= headerSize;
    }

    return false;
}

void Listener::sendCurrentRevision(Client &client)
{
    if (!client.socket || !client.socket->isValid()) {
        return;
    }

    auto command = Akonadi2::CreateRevisionUpdate(m_fbb, m_pipeline->storage().maxRevision());
    Akonadi2::FinishRevisionUpdateBuffer(m_fbb, command);
    Akonadi2::Commands::write(client.socket, ++m_messageId, Akonadi2::Commands::RevisionUpdateCommand, m_fbb);
    m_fbb.Clear();
}

void Listener::sendCommandCompleted(QLocalSocket *socket, uint messageId)
{
    if (!socket || !socket->isValid()) {
        return;
    }

    auto command = Akonadi2::CreateCommandCompletion(m_fbb, messageId);
    Akonadi2::FinishCommandCompletionBuffer(m_fbb, command);
    Akonadi2::Commands::write(socket, ++m_messageId, Akonadi2::Commands::CommandCompletion, m_fbb);
    m_fbb.Clear();
}

void Listener::refreshRevision()
{
    updateClientsWithRevision();
}

void Listener::updateClientsWithRevision()
{
    //FIXME don't send revision updates for revisions that are still being processed.
    auto command = Akonadi2::CreateRevisionUpdate(m_fbb, m_pipeline->storage().maxRevision());
    Akonadi2::FinishRevisionUpdateBuffer(m_fbb, command);

    for (const Client &client: m_connections) {
        if (!client.socket || !client.socket->isValid()) {
            continue;
        }

        Akonadi2::Commands::write(client.socket, ++m_messageId, Akonadi2::Commands::RevisionUpdateCommand, m_fbb);
    }
    m_fbb.Clear();
}

void Listener::loadResource()
{
    if (m_resource) {
        return;
    }

    Akonadi2::ResourceFactory *resourceFactory = Akonadi2::ResourceFactory::load(m_resourceName);
    if (resourceFactory) {
        m_resource = resourceFactory->createResource();
        Log() << QString("Resource factory: %1").arg((qlonglong)resourceFactory);
        Log() << QString("\tResource: %1").arg((qlonglong)m_resource);
        //TODO: this doesn't really list all the facades .. fix
        Log() << "\tFacades: " << Akonadi2::FacadeFactory::instance().getFacade<Akonadi2::ApplicationDomain::Event>(m_resourceName)->type();
        m_resource->configurePipeline(m_pipeline);
    } else {
        ErrorMsg() << "Failed to load resource " << m_resourceName;
    }
    //TODO: on failure ... what?
    //Enter broken state?
}

