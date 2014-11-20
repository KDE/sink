#pragma once

#include <QLocalServer>
#include <QLocalSocket>
#include <QList>
#include <QObject>

class Client
{
public:
    Client(QLocalSocket *s)
        : m_socket(s),
          m_commanded(false)
    {
    }

    QLocalSocket *m_socket;
    bool m_commanded;
};

class Listener : public QObject
{
public:
    Listener(const QString &resourceName, QObject *parent = 0);
    ~Listener();

public Q_SLOTS:
    void closeAllConnections();

private Q_SLOTS:
    void acceptConnection();
    void clientDropped();

private:
    QLocalServer *m_server;
    QList<Client> m_connections;
};