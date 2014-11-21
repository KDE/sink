#include "listener.h"

#include "common/console.h"
#include "common/commands.h"

#include <QLocalSocket>
#include <QTimer>

Listener::Listener(const QString &resource, QObject *parent)
    : QObject(parent),
      m_server(new QLocalServer(this))
{
    connect(m_server, &QLocalServer::newConnection,
             this, &Listener::acceptConnection);
    Console::main()->log(QString("Trying to open %1").arg(resource));
    if (!m_server->listen(resource)) {
        // FIXME: multiple starts need to be handled here
        m_server->removeServer(resource);
        if (!m_server->listen(resource)) {
            Console::main()->log("Utter failure to start server");
            exit(-1);
        }
    }

    if (m_server->isListening()) {
        Console::main()->log(QString("Listening on %1").arg(m_server->serverName()));
    }

    QTimer::singleShot(2000, this, SLOT(checkConnections()));
}

Listener::~Listener()
{
}

void Listener::closeAllConnections()
{
    //TODO: close all client connectionsin m_connections
    for (Client &client: m_connections) {
        if (client.socket) {
            client.socket->close();
            delete client.socket;
            client.socket = 0;
        }
    }
}

void Listener::acceptConnection()
{
    Console::main()->log(QString("Accepting connection"));
    QLocalSocket *socket = m_server->nextPendingConnection();

    if (!socket) {
        return;
    }

    Console::main()->log("Got a connection");
    Client client("Unknown Client" /*fixme: actual names!*/, socket);
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

    Console::main()->log("Dropping connection...");
    QMutableListIterator<Client> it(m_connections);
    while (it.hasNext()) {
        const Client &client = it.next();
        if (client.socket == socket) {
            Console::main()->log(QString("    dropped... %1").arg(client.name));
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

    Console::main()->log("Reading from socket...");
    QMutableListIterator<Client> it(m_connections);
    while (it.hasNext()) {
        Client &client = it.next();
        if (client.socket == socket) {
            Console::main()->log(QString("    Client: %1").arg(client.name));
            client.commandBuffer += socket->readAll();
            processClientBuffer(client);
            break;
        }
    }
}

void Listener::processClientBuffer(Client &client)
{
    static const int headerSize = (sizeof(int) * 2);
    Console::main()->log(QString("processing %1").arg(client.commandBuffer.size()));
    if (client.commandBuffer.size() < headerSize) {
        return;
    }

    int commandId, size;
    commandId = *(int*)client.commandBuffer.constData();
    size = *(int*)(client.commandBuffer.constData() + sizeof(int));

    if (size <= client.commandBuffer.size() - headerSize) {
        QByteArray data = client.commandBuffer.mid(headerSize, size);
        client.commandBuffer.remove(0, headerSize + size);

        switch (commandId) {
            case Commands::HandshakeCommand:
                client.name = data;
                Console::main()->log(QString("    Handshake from %1").arg(client.name));
                //TODO: reply?
                break;
            default:
                // client.hasSentCommand = true;
                break;
        }
    }
}
