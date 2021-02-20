/*
 * Copyright (c) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
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
#include "notification.h"

using namespace Sink;

static QByteArray name(int type)
{
    switch (type) {
        case Notification::Shutdown:
            return "shutdown";
        case Notification::Status:
            return "status";
        case Notification::Info:
            return "info";
        case Notification::Warning:
            return "warning";
        case Notification::Error:
            return "error";
        case Notification::Progress:
            return "progress";
        case Notification::Inspection:
            return "inspection";
        case Notification::RevisionUpdate:
            return "revisionupdate";
        case Notification::FlushCompletion:
            return "flushcompletion";
    }
    return "Unknown:" + QByteArray::number(type);
}

QDebug operator<<(QDebug dbg, const Sink::Notification &n)
{
    dbg << "Notification(Type:" << name(n.type) << ", Id:" << n.id  << ", Code:";
    dbg << n.code;
    dbg << ", Message:" << n.message << ", Entities(" << n.entitiesType << "):" << n.entities << ")";
    return dbg.space();
}
