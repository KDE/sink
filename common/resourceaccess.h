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

#include "sink_export.h"
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

#include <Async/Async>

#include <flatbuffers/flatbuffers.h>
#include "notification.h"
#include "query.h"
#include "log.h"

namespace Sink {

struct QueuedCommand;

class SINK_EXPORT ResourceAccessInterface : public QObject
{
    Q_OBJECT
public:
    typedef QSharedPointer<ResourceAccessInterface> Ptr;

    ResourceAccessInterface()
    {
    }
    virtual ~ResourceAccessInterface()
    {
    }
    virtual KAsync::Job<void> sendCommand(int commandId) = 0;
    virtual KAsync::Job<void> sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb) = 0;
    virtual KAsync::Job<void> synchronizeResource(bool remoteSync, bool localSync) = 0;
    virtual KAsync::Job<void> synchronizeResource(const Sink::QueryBase &filter) = 0;

    virtual KAsync::Job<void> sendCreateCommand(const QByteArray &uid, const QByteArray &resourceBufferType, const QByteArray &buffer)
    {
        return KAsync::null<void>();
    };
    virtual KAsync::Job<void> sendModifyCommand(const QByteArray &uid, qint64 revision, const QByteArray &resourceBufferType, const QByteArrayList &deletedProperties, const QByteArray &buffer, const QByteArrayList &changedProperties)
    {
        return KAsync::null<void>();
    };
    virtual KAsync::Job<void> sendDeleteCommand(const QByteArray &uid, qint64 revision, const QByteArray &resourceBufferType)
    {
        return KAsync::null<void>();
    };
    virtual KAsync::Job<void> sendRevisionReplayedCommand(qint64 revision)
    {
        return KAsync::null<void>();
    };
    virtual KAsync::Job<void>
    sendInspectionCommand(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expecedValue)
    {
        return KAsync::null<void>();
    };

    int getResourceStatus() const
    {
        return mResourceStatus;
    }

signals:
    void ready(bool isReady);
    void revisionChanged(qint64 revision);
    void notification(Notification notification);

public slots:
    virtual void open() = 0;
    virtual void close() = 0;

protected:
    int mResourceStatus;
};

class SINK_EXPORT ResourceAccess : public ResourceAccessInterface
{
    Q_OBJECT
    SINK_DEBUG_AREA("communication")
public:
    typedef QSharedPointer<ResourceAccess> Ptr;

    ResourceAccess(const QByteArray &resourceInstanceIdentifier, const QByteArray &resourceType);
    ~ResourceAccess();

    QByteArray resourceName() const;
    bool isReady() const;

    KAsync::Job<void> sendCommand(int commandId) Q_DECL_OVERRIDE;
    KAsync::Job<void> sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb) Q_DECL_OVERRIDE;
    KAsync::Job<void> synchronizeResource(bool remoteSync, bool localSync) Q_DECL_OVERRIDE;
    KAsync::Job<void> synchronizeResource(const Sink::QueryBase &filter) Q_DECL_OVERRIDE;
    KAsync::Job<void> sendCreateCommand(const QByteArray &uid, const QByteArray &resourceBufferType, const QByteArray &buffer) Q_DECL_OVERRIDE;
    KAsync::Job<void>
    sendModifyCommand(const QByteArray &uid, qint64 revision, const QByteArray &resourceBufferType, const QByteArrayList &deletedProperties, const QByteArray &buffer, const QByteArrayList &changedProperties) Q_DECL_OVERRIDE;
    KAsync::Job<void> sendDeleteCommand(const QByteArray &uid, qint64 revision, const QByteArray &resourceBufferType) Q_DECL_OVERRIDE;
    KAsync::Job<void> sendRevisionReplayedCommand(qint64 revision) Q_DECL_OVERRIDE;
    KAsync::Job<void>
    sendInspectionCommand(int inspectionType,const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expecedValue) Q_DECL_OVERRIDE;
    /**
     * Tries to connect to server, and returns a connected socket on success.
     */
    static KAsync::Job<QSharedPointer<QLocalSocket>> connectToServer(const QByteArray &identifier);

public slots:
    void open() Q_DECL_OVERRIDE;
    void close() Q_DECL_OVERRIDE;

private slots:
    // TODO: move these to the Private class
    void disconnected();
    void connectionError(QLocalSocket::LocalSocketError error);
    void readResourceMessage();
    bool processMessageBuffer();

private:
    void connected();
    void registerCallback(uint messageId, const std::function<void(int error, const QString &)> &callback);

    void sendCommand(const QSharedPointer<QueuedCommand> &command);
    void processCommandQueue();
    void processPendingCommandQueue();

    class Private;
    Private *const d;
    // SINK_DEBUG_COMPONENT(d->resourceInstanceIdentifier)
};

/**
 * A factory for resource access instances that caches the instance for some time.
 *
 * This avoids constantly recreating connections, and should allow a single process to have one connection per resource.
 */
class SINK_EXPORT ResourceAccessFactory
{
    SINK_DEBUG_AREA("ResourceAccessFactory")
public:
    static ResourceAccessFactory &instance();
    Sink::ResourceAccess::Ptr getAccess(const QByteArray &instanceIdentifier, const QByteArray resourceType);

    QHash<QByteArray, QWeakPointer<Sink::ResourceAccess>> mWeakCache;
    QHash<QByteArray, Sink::ResourceAccess::Ptr> mCache;
    QHash<QByteArray, QTimer *> mTimer;
};
}
