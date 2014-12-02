#include "resourceaccess.h"

#include "common/console.h"
#include "common/commands.h"
#include "common/handshake_generated.h"
#include "common/revisionupdate_generated.h"

#include <QDebug>
#include <QProcess>

ResourceAccess::ResourceAccess(const QString &resourceName, QObject *parent)
    : QObject(parent),
      m_resourceName(resourceName),
      m_socket(new QLocalSocket(this)),
      m_tryOpenTimer(new QTimer(this)),
      m_startingProcess(false)
{
    m_tryOpenTimer->setInterval(50);
    m_tryOpenTimer->setSingleShot(true);
    connect(m_tryOpenTimer, &QTimer::timeout,
           this, &ResourceAccess::open);

    Console::main()->log(QString("Starting access to %1").arg(m_socket->serverName()));
    connect(m_socket, &QLocalSocket::connected,
            this, &ResourceAccess::connected);
    connect(m_socket, &QLocalSocket::disconnected,
            this, &ResourceAccess::disconnected);
    connect(m_socket, SIGNAL(error(QLocalSocket::LocalSocketError)),
            this, SLOT(connectionError(QLocalSocket::LocalSocketError)));
    connect(m_socket, &QIODevice::readyRead,
            this, &ResourceAccess::readResourceMessage);

}

ResourceAccess::~ResourceAccess()
{

}

QString ResourceAccess::resourceName() const
{
    return m_resourceName;
}

bool ResourceAccess::isReady() const
{
    return m_socket->isValid();
}

void ResourceAccess::open()
{
    if (m_socket->isValid()) {
        Console::main()->log("Socket valid, so aborting the open");
        return;
    }

    m_socket->setServerName(m_resourceName);
    Console::main()->log(QString("Opening: %1").arg(m_socket->serverName()));
    //FIXME: race between starting the exec and opening the socket?
    m_socket->open();
}

void ResourceAccess::close()
{
    Console::main()->log(QString("Closing: %1").arg(m_socket->fullServerName()));
    m_socket->close();
}

void ResourceAccess::connected()
{
    m_startingProcess = false;
    Console::main()->log(QString("Connected: %1").arg(m_socket->fullServerName()));

    {
        flatbuffers::FlatBufferBuilder fbb;
        auto name = fbb.CreateString("Client PID: " + QString::number((long long)this).toLatin1() + "!");
        auto command = Akonadi::CreateHandshake(fbb, name);
        Akonadi::FinishHandshakeBuffer(fbb, command);
        const int commandId = Commands::HandshakeCommand;
        const int dataSize = fbb.GetSize();
        m_socket->write((const char*)&commandId, sizeof(int));
        m_socket->write((const char*)&dataSize, sizeof(int));
        m_socket->write((const char*)fbb.GetBufferPointer(), dataSize);
    }

    emit ready(true);
}

void ResourceAccess::disconnected()
{
    m_socket->close();
    Console::main()->log(QString("Disconnected: %1").arg(m_socket->fullServerName()));
    emit ready(false);
    open();
}

void ResourceAccess::connectionError(QLocalSocket::LocalSocketError error)
{
    Console::main()->log(QString("Could not connect to %1 due to error %2").arg(m_socket->serverName()).arg(error));
    if (m_startingProcess) {
        if (!m_tryOpenTimer->isActive()) {
            m_tryOpenTimer->start();
        }
        return;
    }

    m_startingProcess = true;
    Console::main()->log(QString("Attempting to start resource..."));
    QStringList args;
    args << m_resourceName;
    if (QProcess::startDetached("akonadinext_resource", args)) {
        m_socket->open();
    }
}

void ResourceAccess::readResourceMessage()
{
    if (!m_socket->isValid()) {
        return;
    }

    m_partialMessageBuffer += m_socket->readAll();

    // should be scheduled rather than processed all at once
    while (processMessageBuffer()) {}
}

bool ResourceAccess::processMessageBuffer()
{
    static const int headerSize = (sizeof(int) * 2);
    Console::main()->log(QString("processing %1").arg(m_partialMessageBuffer.size()));
    if (m_partialMessageBuffer.size() < headerSize) {
        return false;
    }

    const int commandId = *(int*)m_partialMessageBuffer.constData();
    const int size = *(int*)(m_partialMessageBuffer.constData() + sizeof(int));

    if (size > m_partialMessageBuffer.size() - headerSize) {
        return false;
    }

    switch (commandId) {
        case Commands::RevisionUpdateCommand: {
            auto buffer = Akonadi::GetRevisionUpdate(m_partialMessageBuffer.constData() + headerSize);
            Console::main()->log(QString("    Revision updated to: %1").arg(buffer->revision()));
            emit revisionChanged(buffer->revision());
            break;
        }
        default:
            break;
    }

    m_partialMessageBuffer.remove(0, headerSize + size);
    return m_partialMessageBuffer.size() >= headerSize;
}
