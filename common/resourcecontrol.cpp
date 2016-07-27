/*
 * Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "resourcecontrol.h"

#include <QTime>
#include <QUuid>
#include <functional>

#include "resourceaccess.h"
#include "resourceconfig.h"
#include "commands.h"
#include "log.h"
#include "notifier.h"

SINK_DEBUG_AREA("resourcecontrol")

namespace Sink {

KAsync::Job<void> ResourceControl::shutdown(const QByteArray &identifier)
{
    SinkTrace() << "shutdown " << identifier;
    auto time = QSharedPointer<QTime>::create();
    time->start();
    return ResourceAccess::connectToServer(identifier)
        .then<void, QSharedPointer<QLocalSocket>>(
            [identifier, time](const KAsync::Error &error, QSharedPointer<QLocalSocket> socket) {
                if (error) {
                    SinkTrace() << "Resource is already closed.";
                    // Resource isn't started, nothing to shutdown
                    return KAsync::null();
                }
                // We can't currently reuse the socket
                socket->close();
                auto resourceAccess = ResourceAccessFactory::instance().getAccess(identifier, ResourceConfig::getResourceType(identifier));
                resourceAccess->open();
                return resourceAccess->sendCommand(Sink::Commands::ShutdownCommand)
                    .addToContext(resourceAccess)
                    .syncThen<void>([resourceAccess, time]() {
                        resourceAccess->close();
                        SinkTrace() << "Shutdown complete." << Log::TraceTime(time->elapsed());
                    });
            });
}

KAsync::Job<void> ResourceControl::start(const QByteArray &identifier)
{
    SinkTrace() << "start " << identifier;
    auto time = QSharedPointer<QTime>::create();
    time->start();
    auto resourceAccess = ResourceAccessFactory::instance().getAccess(identifier, ResourceConfig::getResourceType(identifier));
    resourceAccess->open();
    return resourceAccess->sendCommand(Sink::Commands::PingCommand).addToContext(resourceAccess).syncThen<void>([time]() { SinkTrace() << "Start complete." << Log::TraceTime(time->elapsed()); });
}

KAsync::Job<void> ResourceControl::flushMessageQueue(const QByteArrayList &resourceIdentifier)
{
    SinkTrace() << "flushMessageQueue" << resourceIdentifier;
    return KAsync::value(resourceIdentifier)
        .template each([](const QByteArray &resource) {
            SinkTrace() << "Flushing message queue " << resource;
            auto resourceAccess = ResourceAccessFactory::instance().getAccess(resource, ResourceConfig::getResourceType(resource));
            resourceAccess->open();
            return resourceAccess->synchronizeResource(false, true)
                .addToContext(resourceAccess);
        });
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
    SinkTrace() << "Sending inspection " << resource;
    auto resourceAccess = ResourceAccessFactory::instance().getAccess(resource, ResourceConfig::getResourceType(resource));
    resourceAccess->open();
    auto notifier = QSharedPointer<Sink::Notifier>::create(resourceAccess);
    auto id = QUuid::createUuid().toByteArray();
    return resourceAccess->sendInspectionCommand(inspectionCommand.type, id, ApplicationDomain::getTypeName<DomainType>(), inspectionCommand.entityIdentifier, inspectionCommand.property, inspectionCommand.expectedValue)
        .template then<void>([resourceAccess, notifier, id, time](KAsync::Future<void> &future) {
            notifier->registerHandler([&future, id, time](const Notification &notification) {
                if (notification.id == id) {
                    SinkTrace() << "Inspection complete." << Log::TraceTime(time->elapsed());
                    if (notification.code) {
                        future.setError(-1, "Inspection returned an error: " + notification.message);
                    } else {
                        future.setFinished();
                    }
                }
            });
        });
}

#define REGISTER_TYPE(T) template KAsync::Job<void> ResourceControl::inspect<T>(const Inspection &);

REGISTER_TYPE(ApplicationDomain::Event);
REGISTER_TYPE(ApplicationDomain::Mail);
REGISTER_TYPE(ApplicationDomain::Folder);
REGISTER_TYPE(ApplicationDomain::SinkResource);

} // namespace Sink
