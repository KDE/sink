#include "listener.h"

#include "common/console.h"
#include "common/commands.h"
#include "common/handshake_generated.h"
#include "common/revisionupdate_generated.h"

#include <QLocalSocket>
#include <QTimer>

Listener::Listener(const QString &resource, QObject *parent)
    : QObject(parent),
      m_server(new QLocalServer(this)),
      m_revision(0)
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

void Listener::setRevision(unsigned long long revision)
{
    if (m_revision != revision) {
        m_revision = revision;
        updateClientsWithRevision();
    }
}

unsigned long long Listener::revision() const
{
    return m_revision;
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
    QMutableVectorIterator<Client> it(m_connections);
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
    for (Client &client: m_connections) {
        if (client.socket == socket) {
            Console::main()->log(QString("    Client: %1").arg(client.name));
            client.commandBuffer += socket->readAll();
            // FIXME: schedule these rather than process them all at once
            //        right now this can lead to starvation of clients due to
            //        one overly active client
            while (processClientBuffer(client)) {}
            break;
        }
    }
}

bool Listener::processClientBuffer(Client &client)
{
    static const int headerSize = (sizeof(int) * 2);
    Console::main()->log(QString("processing %1").arg(client.commandBuffer.size()));
    if (client.commandBuffer.size() < headerSize) {
        return false;
    }

    int commandId, size;
    commandId = *(int*)client.commandBuffer.constData();
    size = *(int*)(client.commandBuffer.constData() + sizeof(int));

    if (size <= client.commandBuffer.size() - headerSize) {
        QByteArray data = client.commandBuffer.mid(headerSize, size);
        client.commandBuffer.remove(0, headerSize + size);

        switch (commandId) {
            case Commands::HandshakeCommand: {
                auto buffer = Akonadi2::GetHandshake(data.constData());
                Console::main()->log(QString("    Handshake from %1").arg(buffer->name()->c_str()));
                sendCurrentRevision(client);
                break;
            }
            default:
                // client.hasSentCommand = true;
                break;
        }

        return client.commandBuffer.size() >= headerSize;
    } else {
        return false;
    }
}

void Listener::sendCurrentRevision(Client &client)
{
    if (!client.socket || !client.socket->isValid()) {
        return;
    }

    auto command = Akonadi2::CreateRevisionUpdate(m_fbb, m_revision);
    Akonadi2::FinishRevisionUpdateBuffer(m_fbb, command);
    Commands::write(client.socket, Commands::RevisionUpdateCommand, m_fbb);
    m_fbb.Clear();
}

void Listener::updateClientsWithRevision()
{
    auto command = Akonadi2::CreateRevisionUpdate(m_fbb, m_revision);
    Akonadi2::FinishRevisionUpdateBuffer(m_fbb, command);

    for (const Client &client: m_connections) {
        if (!client.socket || !client.socket->isValid()) {
            continue;
        }

        Commands::write(client.socket, Commands::RevisionUpdateCommand, m_fbb);
    }
    m_fbb.Clear();
}
