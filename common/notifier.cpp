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
#include "log.h"

using namespace Sink;

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
