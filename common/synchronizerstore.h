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

#include "storage.h"
#include <QByteArrayList>

namespace Sink {

/**
 * A remoteId mapping
 */
class SINK_EXPORT SynchronizerStore
{
public:
    SynchronizerStore(Sink::Storage::DataStore::Transaction &);

    /**
     * Records a localId to remoteId mapping
     */
    void recordRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId);
    void removeRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId);
    void updateRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId);

    /**
     * Tries to find a local id for the remote id, and creates a new local id otherwise.
     *
     * The new local id is recorded in the local to remote id mapping.
     */
    QByteArray resolveRemoteId(const QByteArray &type, const QByteArray &remoteId);

    /**
     * Tries to find a remote id for a local id.
     *
     * This can fail if the entity hasn't been written back to the server yet.
     */
    QByteArray resolveLocalId(const QByteArray &bufferType, const QByteArray &localId);
    QByteArrayList resolveLocalIds(const QByteArray &bufferType, const QByteArrayList &localId);

    void removePrefix(const QByteArray &prefix);
    void removeValue(const QByteArray &prefix, const QByteArray &key);
    QByteArray readValue(const QByteArray &key);
    QByteArray readValue(const QByteArray &prefix, const QByteArray &key);
    void writeValue(const QByteArray &key, const QByteArray &value);
    void writeValue(const QByteArray &prefix, const QByteArray &key, const QByteArray &value);

private:
    Sink::Storage::DataStore::Transaction &mTransaction;
};

}
