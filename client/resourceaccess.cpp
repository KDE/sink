#include "resourceaccess.h"

#include "common/console.h"

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
    if (QProcess::startDetached("toynadi_resource", args)) {
        m_socket->open();
    }
}

