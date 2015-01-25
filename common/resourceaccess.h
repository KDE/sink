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

#include <flatbuffers/flatbuffers.h>
#include <async/src/async.h>

namespace Akonadi2
{

class ResourceAccess : public QObject
{
    Q_OBJECT

public:
    ResourceAccess(const QString &resourceName, QObject *parent = 0);
    ~ResourceAccess();

    QString resourceName() const;
    bool isReady() const;

    Async::Job<void> sendCommand(int commandId);
    Async::Job<void> sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb);
    Async::Job<void> synchronizeResource(bool remoteSync, bool localSync);

public Q_SLOTS:
    void open();
    void close();

Q_SIGNALS:
    void ready(bool isReady);
    void revisionChanged(unsigned long long revision);
    void commandCompleted();

private Q_SLOTS:
    //TODO: move these to the Private class
    void connected();
    void disconnected();
    void connectionError(QLocalSocket::LocalSocketError error);
    void readResourceMessage();
    bool processMessageBuffer();
    void callCallbacks(int id);

private:
    void log(const QString &message);
    void registerCallback(uint messageId, const std::function<void(int error, const QString &)> &callback);

    class Private;
    Private * const d;
};

}
