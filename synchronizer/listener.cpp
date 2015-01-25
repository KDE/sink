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

// commands
#include "common/commandcompletion_generated.h"
#include "common/handshake_generated.h"
#include "common/revisionupdate_generated.h"
#include "common/synchronize_generated.h"

#include <QLocalSocket>
#include <QTimer>

Listener::Listener(const QString &resourceName, QObject *parent)
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
    log(QString("Trying to open %1").arg(resourceName));
    if (!m_server->listen(resourceName)) {
        // FIXME: multiple starts need to be handled here
        m_server->removeServer(resourceName);
        if (!m_server->listen(resourceName)) {
            log("Utter failure to start server");
            exit(-1);
        }
    }

    if (m_server->isListening()) {
        log(QString("Listening on %1").arg(m_server->serverName()));
    }

    //TODO: experiment with different timeouts
    //      or even just drop down to invoking the method queued?
    m_clientBufferProcessesTimer->setInterval(10);
    m_clientBufferProcessesTimer->setSingleShot(true);
    connect(m_clientBufferProcessesTimer, &QTimer::timeout,
            this, &Listener::processClientBuffers);
    QTimer::singleShot(2000, this, SLOT(checkConnections()));
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
    log(QString("Accepting connection"));
    QLocalSocket *socket = m_server->nextPendingConnection();

    if (!socket) {
        return;
    }

    log("Got a connection");
    Client client("Unknown Client", socket);
    connect(socket, &QIODevice::readyRead,
            this, &Listener::readFromSocket);
    m_connections << client;
    connect(socket, &QLocalSocket::disconnected,
            this, &Listener::clientDropped);

}

void Listener::clientDropped()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }

    log("Dropping connection...");
    QMutableVectorIterator<Client> it(m_connections);
    while (it.hasNext()) {
        const Client &client = it.next();
        if (client.socket == socket) {
            log(QString("    dropped... %1").arg(client.name));
            it.remove();
            break;
        }
    }

    checkConnections();
}

void Listener::checkConnections()
{
    if (m_connections.isEmpty()) {
        m_server->close();
        emit noClients();
    }
}

void Listener::readFromSocket()
{
    QLocalSocket *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }

    log("Reading from socket...");
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
                qWarning() << "received invalid command";
            }
            break;
        }
        case Akonadi2::Commands::SynchronizeCommand: {
            flatbuffers::Verifier verifier((const uint8_t *)client.commandBuffer.constData(), size);
            if (Akonadi2::VerifySynchronizeBuffer(verifier)) {
                auto buffer = Akonadi2::GetSynchronize(client.commandBuffer.constData());
                log(QString("\tSynchronize request (id %1) from %2").arg(messageId).arg(client.name));
                loadResource();
                if (!m_resource) {
                    qWarning() << "No resource loaded";
                    break;
                }
                //TODO a more elegant composition of jobs should be possible
                if (buffer->sourceSync()) {
                    bool localSync = buffer->localSync();
                    m_resource->synchronizeWithSource(m_pipeline).then<void>([callback, localSync, this](Async::Future<void> &f){
                        if (localSync) {
                            m_resource->processAllMessages().then<void>([callback](Async::Future<void> &f){
                                callback();
                                f.setFinished();
                            }).exec();
                        } else {
                            callback();
                            f.setFinished();
                        }
                    }).exec();
                } else if (buffer->localSync()) {
                    m_resource->processAllMessages().then<void>([callback](Async::Future<void> &f){
                        callback();
                        f.setFinished();
                    }).exec();
                }
                return;
            } else {
                qWarning() << "received invalid command";
            }
            break;
        }
        case Akonadi2::Commands::FetchEntityCommand:
        case Akonadi2::Commands::DeleteEntityCommand:
        case Akonadi2::Commands::ModifyEntityCommand:
        case Akonadi2::Commands::CreateEntityCommand:
            log(QString("\tCommand id %1 of type %2 from %3").arg(messageId).arg(commandId).arg(client.name));
            loadResource();
            if (m_resource) {
                m_resource->processCommand(commandId, client.commandBuffer, size, m_pipeline);
            }
            break;
        default:
            if (commandId > Akonadi2::Commands::CustomCommand) {
                loadResource();
                if (m_resource) {
                    m_resource->processCommand(commandId, client.commandBuffer, size, m_pipeline);
                }
            } else {
                //TODO: handle error: we don't know wtf this command is
            }
            break;
    }
    callback();
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

        processCommand(commandId, messageId, client, size, [this, messageId, commandId, &client]() {
            log(QString("\tCompleted command messageid %1 of type %2 from %3").arg(messageId).arg(commandId).arg(client.name));
            //FIXME, client needs to become a shared pointer and not a reference, or we have to search through m_connections everytime.
            sendCommandCompleted(client, messageId);
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

void Listener::sendCommandCompleted(Client &client, uint messageId)
{
    if (!client.socket || !client.socket->isValid()) {
        return;
    }

    auto command = Akonadi2::CreateCommandCompletion(m_fbb, messageId);
    Akonadi2::FinishCommandCompletionBuffer(m_fbb, command);
    Akonadi2::Commands::write(client.socket, ++m_messageId, Akonadi2::Commands::CommandCompletion, m_fbb);
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
        log(QString("Resource factory: %1").arg((qlonglong)resourceFactory));
        log(QString("\tResource: %1").arg((qlonglong)m_resource));
        //TODO: this doesn't really list all the facades .. fix
        log(QString("\tFacades: %1").arg(Akonadi2::FacadeFactory::instance().getFacade<Akonadi2::Domain::Event>(m_resourceName)->type()));
        m_resource->configurePipeline(m_pipeline);
    } else {
        log(QString("Failed to load resource %1").arg(m_resourceName));
    }
    //TODO: on failure ... what?
    //Enter broken state?
}

void Listener::log(const QString &message)
{
    qDebug() << "Listener: " << message;
    // Akonadi2::Console::main()->log("Listener: " + message);
}

