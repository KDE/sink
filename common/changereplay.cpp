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
#include "changereplay.h"

#include "entitybuffer.h"
#include "log.h"
#include "definitions.h"
#include "bufferutils.h"

using namespace Sink;

SINK_DEBUG_AREA("changereplay");

ChangeReplay::ChangeReplay(const QByteArray &resourceName)
    : mStorage(storageLocation(), resourceName, Storage::ReadOnly), mChangeReplayStore(storageLocation(), resourceName + ".changereplay", Storage::ReadWrite), mReplayInProgress(false)
{
    SinkTrace() << "Created change replay: " << resourceName;
}

qint64 ChangeReplay::getLastReplayedRevision()
{
    qint64 lastReplayedRevision = 0;
    auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadOnly);
    replayStoreTransaction.openDatabase().scan("lastReplayedRevision",
        [&lastReplayedRevision](const QByteArray &key, const QByteArray &value) -> bool {
            lastReplayedRevision = value.toLongLong();
            return false;
        },
        [](const Storage::Error &) {});
    return lastReplayedRevision;
}

bool ChangeReplay::allChangesReplayed()
{
    const qint64 topRevision = Storage::maxRevision(mStorage.createTransaction(Storage::ReadOnly, [](const Sink::Storage::Error &error) {
        SinkWarning() << error.message;
    }));
    const qint64 lastReplayedRevision = getLastReplayedRevision();
    SinkTrace() << "All changes replayed " << topRevision << lastReplayedRevision;
    return (lastReplayedRevision >= topRevision);
}

KAsync::Job<void> ChangeReplay::replayNextRevision()
{
    mReplayInProgress = true;
    auto mainStoreTransaction = mStorage.createTransaction(Storage::ReadOnly, [](const Sink::Storage::Error &error) {
        SinkWarning() << error.message;
    });
    auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadOnly, [](const Sink::Storage::Error &error) {
        SinkWarning() << error.message;
    });
    qint64 lastReplayedRevision = 0;
    replayStoreTransaction.openDatabase().scan("lastReplayedRevision",
        [&lastReplayedRevision](const QByteArray &key, const QByteArray &value) -> bool {
            lastReplayedRevision = value.toLongLong();
            return false;
        },
        [](const Storage::Error &) {});
    const qint64 topRevision = Storage::maxRevision(mainStoreTransaction);

    auto recordReplayedRevision = [this](qint64 revision) {
        auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadWrite, [](const Sink::Storage::Error &error) {
            SinkWarning() << error.message;
        });
        replayStoreTransaction.openDatabase().write("lastReplayedRevision", QByteArray::number(revision));
        replayStoreTransaction.commit();
    };

    if (lastReplayedRevision < topRevision) {
        SinkTrace() << "Changereplay from " << lastReplayedRevision << " to " << topRevision;
        emit replayingChanges();
        qint64 revision = lastReplayedRevision + 1;
        const auto uid = Storage::getUidFromRevision(mainStoreTransaction, revision);
        const auto type = Storage::getTypeFromRevision(mainStoreTransaction, revision);
        const auto key = Storage::assembleKey(uid, revision);
        KAsync::Job<void> replayJob = KAsync::null<void>();
        Storage::mainDatabase(mainStoreTransaction, type)
            .scan(key,
                [&lastReplayedRevision, type, this, &replayJob](const QByteArray &key, const QByteArray &value) -> bool {
                    SinkTrace() << "Replaying " << key;
                    replayJob = replay(type, key, value);
                    return false;
                },
                [key](const Storage::Error &) { SinkError() << "Failed to replay change " << key; });
        return replayJob.then<void>([this, revision, recordReplayedRevision]() {
            SinkTrace() << "Replayed until " << revision;
            recordReplayedRevision(revision);
            //replay until we're done
            replayNextRevision().exec();
        },
        [this, revision, recordReplayedRevision](int, QString) {
            SinkTrace() << "Change replay failed" << revision;
            //We're probably not online or so, so postpone retrying
            mReplayInProgress = false;
            emit changesReplayed();
        });
    } else {
        SinkTrace() << "No changes to replay";
        mReplayInProgress = false;
        emit changesReplayed();
    }
    return KAsync::null<void>();
}

void ChangeReplay::revisionChanged()
{
    if (!mReplayInProgress) {
        replayNextRevision().exec();
    }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "moc_changereplay.cpp"
#pragma clang diagnostic pop
