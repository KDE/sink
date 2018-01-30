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

#include "sink_export.h"
#include <QObject>

#include <QPointer>
#include <QLocalSocket>
#include <flatbuffers/flatbuffers.h>
#include <log.h>

namespace Sink {
class Resource;
class Notification;
}

class QTimer;
class QLocalServer;

class Client
{
public:
    Client() : socket(nullptr), currentRevision(0)
    {
    }

    Client(const QString &n, QLocalSocket *s) : name(n), socket(s), currentRevision(0)
    {
    }

    QString name;
    QPointer<QLocalSocket> socket;
    QByteArray commandBuffer;
    qint64 currentRevision;
};

class SINK_EXPORT Listener : public QObject
{
    Q_OBJECT
public:
    Listener(const QByteArray &resourceName, const QByteArray &resourceType, QObject *parent = 0);
    ~Listener();

    void checkForUpgrade();

signals:
    void noClients();

public slots:
    void closeAllConnections();
    void emergencyAbortAllConnections();

private slots:
    void acceptConnection();
    void clientDropped();
    void checkConnections();
    void onDataAvailable();
    void processClientBuffers();
    void refreshRevision(qint64);
    void notify(const Sink::Notification &);
    void quit();

private:
    void processCommand(int commandId, uint messageId, const QByteArray &commandBuffer, Client &client, const std::function<void(bool)> &callback);
    bool processClientBuffer(Client &client);
    void sendCommandCompleted(QLocalSocket *socket, uint messageId, bool success);
    void updateClientsWithRevision(qint64);
    Sink::Resource &loadResource();
    void readFromSocket(QLocalSocket *socket);
    void sendShutdownNotification();
    qint64 lowerBoundRevision();

    std::unique_ptr<QLocalServer> m_server;
    QVector<Client> m_connections;
    flatbuffers::FlatBufferBuilder m_fbb;
    const QByteArray m_resourceName;
    const QByteArray m_resourceInstanceIdentifier;
    std::unique_ptr<Sink::Resource> m_resource;
    std::unique_ptr<QTimer> m_clientBufferProcessesTimer;
    std::unique_ptr<QTimer> m_checkConnectionsTimer;
    int m_messageId;
    bool m_exiting;
};
