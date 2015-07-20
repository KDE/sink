/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QTimer>

#include <Async/Async>

#include <flatbuffers/flatbuffers.h>

namespace Akonadi2
{

struct QueuedCommand;

class ResourceAccess : public QObject
{
    Q_OBJECT

public:
    ResourceAccess(const QByteArray &resourceName, QObject *parent = 0);
    ~ResourceAccess();

    QByteArray resourceName() const;
    bool isReady() const;

    KAsync::Job<void> sendCommand(int commandId);
    KAsync::Job<void> sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb);
    KAsync::Job<void> synchronizeResource(bool remoteSync, bool localSync);
    /**
     * Tries to connect to server, and returns a connected socket on success.
     */
    static KAsync::Job<QSharedPointer<QLocalSocket> > connectToServer(const QByteArray &identifier);

public Q_SLOTS:
    void open();
    void close();

Q_SIGNALS:
    void ready(bool isReady);
    void revisionChanged(unsigned long long revision);
    void commandCompleted();

private Q_SLOTS:
    //TODO: move these to the Private class
    void disconnected();
    void connectionError(QLocalSocket::LocalSocketError error);
    void readResourceMessage();
    bool processMessageBuffer();
    void callCallbacks();

private:
    void connected();
    void log(const QString &message);
    void registerCallback(uint messageId, const std::function<void(int error, const QString &)> &callback);

    void sendCommand(const QSharedPointer<QueuedCommand> &command);
    void processCommandQueue();

    class Private;
    Private * const d;
};

}
