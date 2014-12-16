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
#include <QList>
#include <QObject>

#include <flatbuffers/flatbuffers.h>

namespace Akonadi2
{
    class Resource;
}

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
    QLocalSocket *socket;
    QByteArray commandBuffer;
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
    void loadResource();

    QLocalServer *m_server;
    QVector<Client> m_connections;
    unsigned long long m_revision;
    flatbuffers::FlatBufferBuilder m_fbb;
    const QString m_resourceName;
    Akonadi2::Resource *m_resource;
};
