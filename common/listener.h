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

#pragma once

#include <QObject>

#include <QPointer>
#include <QLocalSocket>
#include <flatbuffers/flatbuffers.h>

namespace Akonadi2
{
    class Resource;
}

class QTimer;
class QLocalServer;

class Client
{
public:
    Client()
        : socket(nullptr),
        currentRevision(0)
    {
    }

    Client(const QString &n, QLocalSocket *s)
        : name(n),
          socket(s),
          currentRevision(0)
    {
    }

    QString name;
    QPointer<QLocalSocket> socket;
    QByteArray commandBuffer;
    qint64 currentRevision;
};

class Listener : public QObject
{
    Q_OBJECT

public:
    Listener(const QByteArray &resourceName, QObject *parent = 0);
    ~Listener();

Q_SIGNALS:
    void noClients();

public Q_SLOTS:
    void closeAllConnections();

private Q_SLOTS:
    void acceptConnection();
    void clientDropped();
    void checkConnections();
    void onDataAvailable();
    void processClientBuffers();
    void refreshRevision(qint64);
    void quit();

private:
    void processCommand(int commandId, uint messageId, const QByteArray &commandBuffer, Client &client, const std::function<void()> &callback);
    bool processClientBuffer(Client &client);
    void sendCommandCompleted(QLocalSocket *socket, uint messageId);
    void updateClientsWithRevision(qint64);
    void loadResource();
    void readFromSocket(QLocalSocket *socket);
    qint64 lowerBoundRevision();

    QLocalServer *m_server;
    QVector<Client> m_connections;
    flatbuffers::FlatBufferBuilder m_fbb;
    const QByteArray m_resourceName;
    const QByteArray m_resourceInstanceIdentifier;
    Akonadi2::Resource *m_resource;
    QTimer *m_clientBufferProcessesTimer;
    QTimer *m_checkConnectionsTimer;
    int m_messageId;
};
