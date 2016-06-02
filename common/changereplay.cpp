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

#undef DEBUG_AREA
#define DEBUG_AREA "resource.changereplay"

ChangeReplay::ChangeReplay(const QByteArray &resourceName)
    : mStorage(storageLocation(), resourceName, Storage::ReadOnly), mChangeReplayStore(storageLocation(), resourceName + ".changereplay", Storage::ReadWrite), mReplayInProgress(false)
{
    Trace() << "Created change replay: " << resourceName;
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
        Warning() << error.message;
    }));
    const qint64 lastReplayedRevision = getLastReplayedRevision();
    Trace() << "All changes replayed " << topRevision << lastReplayedRevision;
    return (lastReplayedRevision >= topRevision);
}

KAsync::Job<void> ChangeReplay::replayNextRevision()
{
    mReplayInProgress = true;
    auto mainStoreTransaction = mStorage.createTransaction(Storage::ReadOnly, [](const Sink::Storage::Error &error) {
        Warning() << error.message;
    });
    auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadOnly, [](const Sink::Storage::Error &error) {
        Warning() << error.message;
    });
    qint64 lastReplayedRevision = 0;
    replayStoreTransaction.openDatabase().scan("lastReplayedRevision",
        [&lastReplayedRevision](const QByteArray &key, const QByteArray &value) -> bool {
            lastReplayedRevision = value.toLongLong();
            return false;
        },
        [](const Storage::Error &) {});
    const qint64 topRevision = Storage::maxRevision(mainStoreTransaction);

    if (lastReplayedRevision < topRevision) {
        Trace() << "Changereplay from " << lastReplayedRevision << " to " << topRevision;
        qint64 revision = lastReplayedRevision + 1;
        const auto uid = Storage::getUidFromRevision(mainStoreTransaction, revision);
        const auto type = Storage::getTypeFromRevision(mainStoreTransaction, revision);
        const auto key = Storage::assembleKey(uid, revision);
        KAsync::Job<void> replayJob = KAsync::null<void>();
        Storage::mainDatabase(mainStoreTransaction, type)
            .scan(key,
                [&lastReplayedRevision, type, this, &replayJob](const QByteArray &key, const QByteArray &value) -> bool {
                    Trace() << "Replaying " << key;
                    replayJob = replay(type, key, value);
                    // TODO make for loop async, and pass to async replay function together with type
                    return false;
                },
                [key](const Storage::Error &) { ErrorMsg() << "Failed to replay change " << key; });
        return replayJob.then<void>([this, revision]() {
            auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadWrite, [](const Sink::Storage::Error &error) {
                Warning() << error.message;
            });
            replayStoreTransaction.openDatabase().write("lastReplayedRevision", QByteArray::number(revision));
            replayStoreTransaction.commit();
            Trace() << "Replayed until " << revision;
        }).then<void>([this]() {
            //replay until we're done
            replayNextRevision().exec();
        });
    } else {
        Trace() << "No changes to replay";
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

