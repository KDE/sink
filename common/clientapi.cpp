/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "clientapi.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>
#include <QTime>
#include <QEventLoop>
#include <QAbstractItemModel>
#include <QDir>
#include <QUuid>
#include <functional>
#include <memory>

#include "resourceaccess.h"
#include "commands.h"
#include "resourcefacade.h"
#include "definitions.h"
#include "resourceconfig.h"
#include "facadefactory.h"
#include "modelresult.h"
#include "storage.h"
#include "log.h"

#undef DEBUG_AREA
#define DEBUG_AREA "client.clientapi"

namespace Sink
{

KAsync::Job<void> ResourceControl::shutdown(const QByteArray &identifier)
{
    Trace() << "shutdown " << identifier;
    auto time = QSharedPointer<QTime>::create();
    time->start();
    return ResourceAccess::connectToServer(identifier).then<void, QSharedPointer<QLocalSocket>>([identifier, time](QSharedPointer<QLocalSocket> socket, KAsync::Future<void> &future) {
        //We can't currently reuse the socket
        socket->close();
        auto resourceAccess = QSharedPointer<Sink::ResourceAccess>::create(identifier);
        resourceAccess->open();
        resourceAccess->sendCommand(Sink::Commands::ShutdownCommand).then<void>([&future, resourceAccess, time]() {
            Trace() << "Shutdown complete." << Log::TraceTime(time->elapsed());
            future.setFinished();
        }).exec();
    },
    [](int, const QString &) {
        Trace() << "Resource is already closed.";
        //Resource isn't started, nothing to shutdown
    })
    //FIXME JOBAPI this is only required because we don't care about the return value of connectToServer
    .template then<void>([](){});
}

KAsync::Job<void> ResourceControl::start(const QByteArray &identifier)
{
    Trace() << "start " << identifier;
    auto time = QSharedPointer<QTime>::create();
    time->start();
    auto resourceAccess = QSharedPointer<Sink::ResourceAccess>::create(identifier);
    resourceAccess->open();
    return resourceAccess->sendCommand(Sink::Commands::PingCommand).then<void>([resourceAccess, time]() {
        Trace() << "Start complete." << Log::TraceTime(time->elapsed());
    });
}

KAsync::Job<void> ResourceControl::flushMessageQueue(const QByteArrayList &resourceIdentifier)
{
    Trace() << "flushMessageQueue" << resourceIdentifier;
    return KAsync::iterate(resourceIdentifier)
    .template each<void, QByteArray>([](const QByteArray &resource, KAsync::Future<void> &future) {
        Trace() << "Flushing message queue " << resource;
        auto resourceAccess = QSharedPointer<Sink::ResourceAccess>::create(resource);
        resourceAccess->open();
        resourceAccess->synchronizeResource(false, true).then<void>([&future, resourceAccess]() {
            future.setFinished();
        }).exec();
    })
    //FIXME JOBAPI this is only required because we don't care about the return value of each (and each shouldn't even have a return value)
    .template then<void>([](){});
}

KAsync::Job<void> ResourceControl::flushReplayQueue(const QByteArrayList &resourceIdentifier)
{
    return flushMessageQueue(resourceIdentifier);
}

template <class DomainType>
KAsync::Job<void> ResourceControl::inspect(const Inspection &inspectionCommand)
{
    auto resource = inspectionCommand.resourceIdentifier;

    auto time = QSharedPointer<QTime>::create();
    time->start();
    Trace() << "Sending inspection " << resource;
    auto resourceAccess = QSharedPointer<Sink::ResourceAccess>::create(resource);
    resourceAccess->open();
    auto notifier = QSharedPointer<Sink::Notifier>::create(resourceAccess);
    auto id = QUuid::createUuid().toByteArray();
    return resourceAccess->sendInspectionCommand(id, ApplicationDomain::getTypeName<DomainType>(), inspectionCommand.entityIdentifier, inspectionCommand.property, inspectionCommand.expectedValue)
        .template then<void>([resourceAccess, notifier, id, time](KAsync::Future<void> &future) {
            notifier->registerHandler([&future, id, time](const Notification &notification) {
                if (notification.id == id) {
                    Trace() << "Inspection complete." << Log::TraceTime(time->elapsed());
                    if (notification.code) {
                        future.setError(-1, "Inspection returned an error: " + notification.message);
                    } else {
                        future.setFinished();
                    }
                }
            });
        });
}

class Sink::Notifier::Private {
public:
    Private()
        : context(new QObject)
    {

    }
    QList<QSharedPointer<ResourceAccess> > resourceAccess;
    QList<std::function<void(const Notification &)> > handler;
    QSharedPointer<QObject> context;
};

Notifier::Notifier(const QSharedPointer<ResourceAccess> &resourceAccess)
    : d(new Sink::Notifier::Private)
{
    QObject::connect(resourceAccess.data(), &ResourceAccess::notification, d->context.data(), [this](const Notification &notification) {
        for (const auto &handler : d->handler) {
            handler(notification);
        }
    });
    d->resourceAccess << resourceAccess;
}

Notifier::Notifier(const QByteArray &instanceIdentifier)
    : d(new Sink::Notifier::Private)
{
    auto resourceAccess = Sink::ResourceAccess::Ptr::create(instanceIdentifier);
    resourceAccess->open();
    QObject::connect(resourceAccess.data(), &ResourceAccess::notification, d->context.data(), [this](const Notification &notification) {
        for (const auto &handler : d->handler) {
            handler(notification);
        }
    });
    d->resourceAccess << resourceAccess;
}

void Notifier::registerHandler(std::function<void(const Notification &)> handler)
{
    d->handler << handler;
}

#define REGISTER_TYPE(T) template KAsync::Job<void> ResourceControl::inspect<T>(const Inspection &); \

REGISTER_TYPE(ApplicationDomain::Event);
REGISTER_TYPE(ApplicationDomain::Mail);
REGISTER_TYPE(ApplicationDomain::Folder);
REGISTER_TYPE(ApplicationDomain::SinkResource);

} // namespace Sink

