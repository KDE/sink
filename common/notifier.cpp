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

#include "notifier.h"

#include <functional>

#include "resourceaccess.h"
#include "resourceconfig.h"
#include "query.h"
#include "facadefactory.h"
#include "log.h"

using namespace Sink;

class Sink::Notifier::Private
{
public:
    Private()
    {
    }

    void listenForNotifications(const QSharedPointer<ResourceAccess> &access)
    {
        QObject::connect(access.data(), &ResourceAccess::notification, &context, [this](const Notification &notification) {
            for (const auto &h : handler) {
                h(notification);
            }
        });
        resourceAccess << access;
    }

    QList<QSharedPointer<ResourceAccess>> resourceAccess;
    QList<std::function<void(const Notification &)>> handler;
    QSharedPointer<Sink::ResultEmitter<QSharedPointer<Sink::ApplicationDomain::SinkResource> > > mResourceEmitter;
    QObject context;
};

Notifier::Notifier(const QSharedPointer<ResourceAccess> &resourceAccess) : d(new Sink::Notifier::Private)
{
    d->listenForNotifications(resourceAccess);
}

Notifier::Notifier(const QByteArray &instanceIdentifier, const QByteArray &resourceType) : d(new Sink::Notifier::Private)
{
    auto resourceAccess = Sink::ResourceAccessFactory::instance().getAccess(instanceIdentifier, resourceType);
    resourceAccess->open();
    d->listenForNotifications(resourceAccess);
}

Notifier::Notifier(const QByteArray &instanceIdentifier) : Notifier(instanceIdentifier, ResourceConfig::getResourceType(instanceIdentifier))
{
}

Notifier::Notifier(const Sink::Query &resourceQuery) : d(new Sink::Notifier::Private)
{
    Sink::Log::Context resourceCtx{"notifier"};
    auto facade = FacadeFactory::instance().getFacade<ApplicationDomain::SinkResource>();
    Q_ASSERT(facade);

    auto result = facade->load(resourceQuery, resourceCtx);
    auto emitter = result.second;
    emitter->onAdded([=](const ApplicationDomain::SinkResource::Ptr &resource) {
        auto resourceAccess = Sink::ResourceAccessFactory::instance().getAccess(resource->identifier(), ResourceConfig::getResourceType(resource->identifier()));
        resourceAccess->open();
        d->listenForNotifications(resourceAccess);
    });
    emitter->onComplete([resourceCtx]() {
        SinkTraceCtx(resourceCtx) << "Resource query complete";
    });
    emitter->fetch();
    if (resourceQuery.liveQuery()) {
        d->mResourceEmitter = emitter;
    }
    result.first.exec();
}

void Notifier::registerHandler(std::function<void(const Notification &)> handler)
{
    d->handler << handler;
}
