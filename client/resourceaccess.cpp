#include "resourceaccess.h"

#include "common/console.h"

#include <QDebug>
#include <QProcess>

ResourceAccess::ResourceAccess(const QString &resourceName, QObject *parent)
    : QObject(parent),
      m_resourceName(resourceName),
      m_socket(new QLocalSocket(this)),
      m_startingProcess(false)
{
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
    static int count = 0;
    if (m_startingProcess) {
        QMetaObject::invokeMethod(this, "open", Qt::QueuedConnection);
    }

    if (m_socket->isValid()) {
        return;
    }

    ++count;
    if (count > 10000) {
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
    Console::main()->log(QString("Disconnected: %1").arg(m_socket->fullServerName()));
    emit ready(false);
}

void ResourceAccess::connectionError(QLocalSocket::LocalSocketError error)
{
    Console::main()->log(QString("Could not connect to %1 due to error %2").arg(m_socket->serverName()).arg(error));
    if (m_startingProcess) {
        QMetaObject::invokeMethod(this, "open", Qt::QueuedConnection);
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

