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

#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>

#include <flatbuffers/flatbuffers.h>

#include "common/pipeline.h"

namespace Akonadi2
{
    class Resource;
}

class QTimer;

class Client
{
public:
    Client()
        : socket(nullptr)
    {
    }

    Client(const QString &n, QLocalSocket *s)
        : name(n),
          socket(s)
    {
    }

    QString name;
    QPointer<QLocalSocket> socket;
    QByteArray commandBuffer;
};

class QLockFile;

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
    void refreshRevision();
    void quit();

private:
    void processCommand(int commandId, uint messageId, Client &client, uint size, const std::function<void()> &callback);
    bool processClientBuffer(Client &client);
    void sendCurrentRevision(Client &client);
    void sendCommandCompleted(QLocalSocket *socket, uint messageId);
    void updateClientsWithRevision();
    void loadResource();
    void readFromSocket(QLocalSocket *socket);

    QLocalServer *m_server;
    QVector<Client> m_connections;
    flatbuffers::FlatBufferBuilder m_fbb;
    const QByteArray m_resourceName;
    const QByteArray m_resourceInstanceIdentifier;
    Akonadi2::Resource *m_resource;
    Akonadi2::Pipeline *m_pipeline;
    QTimer *m_clientBufferProcessesTimer;
    QTimer *m_checkConnectionsTimer;
    int m_messageId;
    QLockFile *m_lockfile;
};
