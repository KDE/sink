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
#include "remoteidmap.h"

#include <QUuid>
#include "index.h"
#include "log.h"

using namespace Sink;

SINK_DEBUG_AREA("remoteidmap")

RemoteIdMap::RemoteIdMap(Sink::Storage::Transaction &transaction)
    : mTransaction(transaction)
{

}

void RemoteIdMap::recordRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId)
{
    Index("rid.mapping." + bufferType, mTransaction).add(remoteId, localId);
    Index("localid.mapping." + bufferType, mTransaction).add(localId, remoteId);
}

void RemoteIdMap::removeRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId)
{
    Index("rid.mapping." + bufferType, mTransaction).remove(remoteId, localId);
    Index("localid.mapping." + bufferType, mTransaction).remove(localId, remoteId);
}

void RemoteIdMap::updateRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId)
{
    const auto oldRemoteId = Index("localid.mapping." + bufferType, mTransaction).lookup(localId);
    removeRemoteId(bufferType, localId, oldRemoteId);
    recordRemoteId(bufferType, localId, remoteId);
}

QByteArray RemoteIdMap::resolveRemoteId(const QByteArray &bufferType, const QByteArray &remoteId)
{
    // Lookup local id for remote id, or insert a new pair otherwise
    Index index("rid.mapping." + bufferType, mTransaction);
    QByteArray sinkId = index.lookup(remoteId);
    if (sinkId.isEmpty()) {
        sinkId = Sink::Storage::generateUid();
        index.add(remoteId, sinkId);
        Index("localid.mapping." + bufferType, mTransaction).add(sinkId, remoteId);
    }
    return sinkId;
}

QByteArray RemoteIdMap::resolveLocalId(const QByteArray &bufferType, const QByteArray &localId)
{
    QByteArray remoteId = Index("localid.mapping." + bufferType, mTransaction).lookup(localId);
    if (remoteId.isEmpty()) {
        SinkWarning() << "Couldn't find the remote id for " << localId;
        return QByteArray();
    }
    return remoteId;
}

QByteArray RemoteIdMap::readValue(const QByteArray &key)
{
    QByteArray value;
    mTransaction.openDatabase("values").scan(key, [&value](const QByteArray &, const QByteArray &v) {
        value = v;
        return false;
    }, [](const Sink::Storage::Error &) {
        //Ignore errors because we may not find the value
    });
    return value;
}

void RemoteIdMap::writeValue(const QByteArray &key, const QByteArray &value)
{
    mTransaction.openDatabase("values").write(key, value);
}

