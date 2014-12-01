#pragma once

#include <QLocalServer>
#include <QLocalSocket>
#include <QList>
#include <QObject>

class Client
{
public:
    Client()
        : socket(nullptr),
          hasSentCommand(false)
    {
    }

    Client(const QString &n, QLocalSocket *s)
        : name(n),
          socket(s),
          hasSentCommand(false)
    {
    }

    QString name;
    QLocalSocket *socket;
    QByteArray commandBuffer;
    bool hasSentCommand;
};

class Listener : public QObject
{
    Q_OBJECT

public:
    Listener(const QString &resourceName, QObject *parent = 0);
    ~Listener();

    void setRevision(unsigned long long revision);
    unsigned long long revision() const;

Q_SIGNALS:
    void noClients();

public Q_SLOTS:
    void closeAllConnections();

private Q_SLOTS:
    void acceptConnection();
    void clientDropped();
    void checkConnections();
    void readFromSocket();

private:
    bool processClientBuffer(Client &client);
    void sendCurrentRevision(Client &client);
    void updateClientsWithRevision();
    QLocalServer *m_server;
    QVector<Client> m_connections;
    unsigned long long m_revision;
};