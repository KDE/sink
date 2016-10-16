/*
 * Copyright (C) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
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

#include "storage/entitystore.h"

namespace Sink {

class SINK_EXPORT EntityStore
{
public:
    EntityStore(Storage::EntityStore &store);

    template<typename T>
    T read(const QByteArray &identifier) const
    {
        return store.readLatest<T>(identifier);
    }

    template<typename T>
    T readFromKey(const QByteArray &key) const
    {
        return store.readEntity<T>(key);
    }

    template<typename T>
    T readPrevious(const QByteArray &uid, qint64 revision) const
    {
        return store.readPrevious<T>(uid, revision);
    }

    /* template<typename T> */
    /* EntityReader<T> reader() */
    /* { */
    /*     return EntityReader<T>(mResourceType, mResourceInstanceIdentifier, mTransaction); */
    /* } */

private:
    Sink::Storage::EntityStore &store;
};

}
