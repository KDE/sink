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
#include "synchronizerstore.h"

#include "index.h"
#include "log.h"

using namespace Sink;

SynchronizerStore::SynchronizerStore(Sink::Storage::DataStore::Transaction &transaction)
    : mTransaction(transaction)
{

}

void SynchronizerStore::recordRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId)
{
    Index("rid.mapping." + bufferType, mTransaction).add(remoteId, localId);
    Index("localid.mapping." + bufferType, mTransaction).add(localId, remoteId);
}

void SynchronizerStore::removeRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId)
{
    Index("rid.mapping." + bufferType, mTransaction).remove(remoteId, localId);
    Index("localid.mapping." + bufferType, mTransaction).remove(localId, remoteId);
}

void SynchronizerStore::updateRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId)
{
    const auto oldRemoteId = Index("localid.mapping." + bufferType, mTransaction).lookup(localId);
    removeRemoteId(bufferType, localId, oldRemoteId);
    recordRemoteId(bufferType, localId, remoteId);
}

QByteArray SynchronizerStore::resolveRemoteId(const QByteArray &bufferType, const QByteArray &remoteId)
{
    if (remoteId.isEmpty()) {
        SinkWarning() << "Cannot resolve empty remote id for type: " << bufferType;
        return QByteArray();
    }
    // Lookup local id for remote id, or insert a new pair otherwise
    Index index("rid.mapping." + bufferType, mTransaction);
    QByteArray sinkId = index.lookup(remoteId);
    if (sinkId.isEmpty()) {
        sinkId = Sink::Storage::DataStore::generateUid();
        index.add(remoteId, sinkId);
        Index("localid.mapping." + bufferType, mTransaction).add(sinkId, remoteId);
    }
    return sinkId;
}

QByteArray SynchronizerStore::resolveLocalId(const QByteArray &bufferType, const QByteArray &localId)
{
    if (localId.isEmpty()) {
        SinkError() << "Tried to resolve an empty local id";
        Q_ASSERT(false);
        return {};
    }
    QByteArray remoteId = Index("localid.mapping." + bufferType, mTransaction).lookup(localId);
    if (remoteId.isEmpty()) {
        //This can happen if we didn't store the remote id in the first place
        SinkTrace() << "Couldn't find the remote id for " << bufferType << localId;
        return QByteArray();
    }
    return remoteId;
}

QByteArrayList SynchronizerStore::resolveLocalIds(const QByteArray &bufferType, const QByteArrayList &localIds)
{
    QByteArrayList result;
    for (const auto &l : localIds) {
        const auto id = resolveLocalId(bufferType, l);
        if (!id.isEmpty()) {
            result << id;
        }
    }
    return result;
}

QByteArray SynchronizerStore::readValue(const QByteArray &key)
{
    QByteArray value;
    mTransaction.openDatabase("values").scan(key, [&value](const QByteArray &, const QByteArray &v) {
        value = v;
        return false;
    }, [](const Sink::Storage::DataStore::Error &) {
        //Ignore errors because we may not find the value
    });
    return value;
}

QByteArray SynchronizerStore::readValue(const QByteArray &prefix, const QByteArray &key)
{
    return readValue(prefix + key);
}

void SynchronizerStore::writeValue(const QByteArray &key, const QByteArray &value)
{
    mTransaction.openDatabase("values").write(key, value);
}

void SynchronizerStore::writeValue(const QByteArray &prefix, const QByteArray &key, const QByteArray &value)
{
    writeValue(prefix + key, value);
}

void SynchronizerStore::removeValue(const QByteArray &prefix, const QByteArray &key)
{
    auto assembled = prefix + key;
    if (assembled.isEmpty()) {
        return;
    }
    mTransaction.openDatabase("values").remove(assembled, [&](const Sink::Storage::DataStore::Error &error) {
        SinkWarning() << "Failed to remove the value: " << prefix + key << error;
    });
}

void SynchronizerStore::removePrefix(const QByteArray &prefix)
{
    if (prefix.isEmpty()) {
        return;
    }
    auto db = mTransaction.openDatabase("values");
    QByteArrayList keys;
    db.scan(prefix, [&] (const QByteArray &key, const QByteArray &value) {
        keys << key;
        return true;
    }, {}, true, true);
    for (const auto &k : keys) {
        db.remove(k);
    }
}

