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
#include <functional>

#include "resourceaccess.h"
#include "resourceconfig.h"
#include "commands.h"
#include "log.h"
#include "notifier.h"
#include "utils.h"

namespace Sink {

KAsync::Job<void> ResourceControl::shutdown(const QByteArray &identifier)
{
    const auto ctx = Log::Context{identifier + ".resourcecontrol"};
    SinkTraceCtx(ctx) << "shutdown " << identifier;
    auto time = QSharedPointer<QTime>::create();
    time->start();

    auto resourceAccess = ResourceAccessFactory::instance().getAccess(identifier, ResourceConfig::getResourceType(identifier));
    return resourceAccess->shutdown()
        .addToContext(resourceAccess)
        .then<void>([resourceAccess, time, ctx](KAsync::Future<void> &future) {
            SinkTraceCtx(ctx) << "Shutdown command complete, waiting for shutdown." << Log::TraceTime(time->elapsed());
            if (!resourceAccess->isReady()) {
                future.setFinished();
                return;
            }
            auto guard = new QObject;
            QObject::connect(resourceAccess.data(), &ResourceAccess::ready, guard, [&future, guard](bool ready) {
                if (!ready) {
                    //Protect against callback getting called twice.
                    delete guard;
                    future.setFinished();
                }
            });
        }).then([time, ctx] {
            SinkTraceCtx(ctx) << "Shutdown complete." << Log::TraceTime(time->elapsed());
        });
}

KAsync::Job<void> ResourceControl::start(const QByteArray &identifier)
{
    SinkTrace() << "start " << identifier;
    auto time = QSharedPointer<QTime>::create();
    time->start();
    auto resourceAccess = ResourceAccessFactory::instance().getAccess(identifier, ResourceConfig::getResourceType(identifier));
    resourceAccess->open();
    return resourceAccess->sendCommand(Sink::Commands::PingCommand).addToContext(resourceAccess).then([time]() { SinkTrace() << "Start complete." << Log::TraceTime(time->elapsed()); });
}

KAsync::Job<void> ResourceControl::flushMessageQueue(const QByteArrayList &resourceIdentifier)
{
    SinkTrace() << "flushMessageQueue" << resourceIdentifier;
    return KAsync::value(resourceIdentifier)
        .template each([](const QByteArray &resource) {
            return flushMessageQueue(resource);
        });
}

KAsync::Job<void> ResourceControl::flushMessageQueue(const QByteArray &resourceIdentifier)
{
    return flush(Flush::FlushUserQueue, resourceIdentifier).then(flush(Flush::FlushSynchronization, resourceIdentifier));
}

KAsync::Job<void> ResourceControl::flush(Flush::FlushType type, const QByteArray &resourceIdentifier)
{
    auto resourceAccess = ResourceAccessFactory::instance().getAccess(resourceIdentifier, ResourceConfig::getResourceType(resourceIdentifier));
    auto notifier = QSharedPointer<Sink::Notifier>::create(resourceAccess);
    auto id = createUuid();
    return KAsync::start<void>([=](KAsync::Future<void> &future) {
            SinkLog() << "Starting flush " << id;
            notifier->registerHandler([&future, id](const Notification &notification) {
                SinkTrace() << "Received notification: " << notification.type << notification.id;
                if (notification.type == Notification::Error && notification.code == ApplicationDomain::ResourceCrashedError) {
                    SinkWarning() << "Error during flush";
                    future.setError(-1, "Error during flush: " + notification.message);
                } else if (notification.id == id) {
                    SinkTrace() << "FlushComplete";
                    if (notification.code) {
                        SinkWarning() << "Flush returned an error";
                        future.setError(-1, "Flush returned an error: " + notification.message);
                    } else {
                        future.setFinished();
                    }
                }
            });
            resourceAccess->sendFlushCommand(type, id).onError([&future] (const KAsync::Error &error) {
                SinkWarning() << "Failed to send command";
                future.setError(1, "Failed to send command: " + error.errorMessage);
            }).exec();
        });
}

KAsync::Job<void> ResourceControl::flushReplayQueue(const QByteArrayList &resourceIdentifier)
{
    return KAsync::value(resourceIdentifier)
        .template each([](const QByteArray &resource) {
            return flushReplayQueue(resource);
        });
}

KAsync::Job<void> ResourceControl::flushReplayQueue(const QByteArray &resourceIdentifier)
{
    return flush(Flush::FlushReplayQueue, resourceIdentifier);
}

KAsync::Job<void> ResourceControl::inspect(const Inspection &inspectionCommand, const QByteArray &domainType)
{
    auto resourceIdentifier = inspectionCommand.resourceIdentifier;
    auto resourceAccess = ResourceAccessFactory::instance().getAccess(resourceIdentifier, ResourceConfig::getResourceType(resourceIdentifier));
    auto notifier = QSharedPointer<Sink::Notifier>::create(resourceAccess);
    auto id = createUuid();
    return KAsync::start<void>([=](KAsync::Future<void> &future) {
            notifier->registerHandler([&future, id](const Notification &notification) {
                if (notification.id == id) {
                    SinkTrace() << "Inspection complete";
                    if (notification.code) {
                        SinkWarning() << "Inspection returned an error";
                        future.setError(-1, "Inspection returned an error: " + notification.message);
                    } else {
                        future.setFinished();
                    }
                }
            });
            resourceAccess->sendInspectionCommand(inspectionCommand.type, id, domainType, inspectionCommand.entityIdentifier, inspectionCommand.property, inspectionCommand.expectedValue).onError([&future] (const KAsync::Error &error) {
                SinkWarning() << "Failed to send command";
                future.setError(1, "Failed to send command: " + error.errorMessage);
            }).exec();
        });
}


} // namespace Sink
