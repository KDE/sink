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

#include <QTimer>

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

void ChangeReplay::recordReplayedRevision(qint64 revision)
{
    auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadWrite, [](const Sink::Storage::Error &error) {
        SinkWarning() << error.message;
    });
    replayStoreTransaction.openDatabase().write("lastReplayedRevision", QByteArray::number(revision));
    replayStoreTransaction.commit();
};

KAsync::Job<void> ChangeReplay::replayNextRevision()
{
    auto lastReplayedRevision = QSharedPointer<qint64>::create(0);
    auto topRevision = QSharedPointer<qint64>::create(0);
    return KAsync::syncStart<void>([this, lastReplayedRevision, topRevision]() {
            mReplayInProgress = true;
            mMainStoreTransaction = mStorage.createTransaction(Storage::ReadOnly, [](const Sink::Storage::Error &error) {
                SinkWarning() << error.message;
            });
            auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadOnly, [](const Sink::Storage::Error &error) {
                SinkWarning() << error.message;
            });
            replayStoreTransaction.openDatabase().scan("lastReplayedRevision",
                [lastReplayedRevision](const QByteArray &key, const QByteArray &value) -> bool {
                    *lastReplayedRevision = value.toLongLong();
                    return false;
                },
                [](const Storage::Error &) {});
            *topRevision = Storage::maxRevision(mMainStoreTransaction);
            SinkTrace() << "Changereplay from " << *lastReplayedRevision << " to " << *topRevision;
        })
        .then(KAsync::dowhile(
            [this, lastReplayedRevision, topRevision]() -> KAsync::Job<KAsync::ControlFlowFlag> {
                    if (*lastReplayedRevision >= *topRevision) {
                        return KAsync::value(KAsync::Break);
                    }

                    qint64 revision = *lastReplayedRevision + 1;
                    KAsync::Job<void> replayJob = KAsync::null<void>();
                    while (revision <= *topRevision) {
                        const auto uid = Storage::getUidFromRevision(mMainStoreTransaction, revision);
                        const auto type = Storage::getTypeFromRevision(mMainStoreTransaction, revision);
                        const auto key = Storage::assembleKey(uid, revision);
                        bool exitLoop = false;
                        Storage::mainDatabase(mMainStoreTransaction, type)
                            .scan(key,
                                [&lastReplayedRevision, type, this, &replayJob, &exitLoop, revision](const QByteArray &key, const QByteArray &value) -> bool {
                                    SinkTrace() << "Replaying " << key;
                                    if (canReplay(type, key, value)) {
                                        replayJob = replay(type, key, value).then<void>([this, revision, lastReplayedRevision](const KAsync::Error &error) {
                                            if (error) {
                                                SinkTrace() << "Change replay failed" << revision;
                                                return KAsync::error(error);
                                            } else {
                                                recordReplayedRevision(revision);
                                                *lastReplayedRevision = revision;
                                            }
                                            return KAsync::null();
                                        });
                                        exitLoop = true;
                                    } else {
                                        *lastReplayedRevision = revision;
                                    }
                                    return false;
                                },
                                [key](const Storage::Error &) { SinkError() << "Failed to replay change " << key; });
                        if (exitLoop) {
                            break;
                        }
                        revision++;
                    }
                    return replayJob.then<KAsync::ControlFlowFlag>([this, revision, lastReplayedRevision, topRevision](const KAsync::Error &error) ->KAsync::Job<KAsync::ControlFlowFlag> {
                        if (error) {
                            SinkTrace() << "Change replay failed" << revision;
                            //We're probably not online or so, so postpone retrying
                            return KAsync::value(KAsync::Break);
                        } else {
                            SinkTrace() << "Replayed until " << revision;
                            recordReplayedRevision(*lastReplayedRevision);
                            if (*lastReplayedRevision < *topRevision) {
                                return KAsync::wait(0).then(KAsync::value(KAsync::Continue));
                            } else {
                                return KAsync::value(KAsync::Break);
                            }
                        }
                        //We shouldn't ever get here
                        Q_ASSERT(false);
                        return KAsync::value(KAsync::Break);
                    });
            }))
        .syncThen<void>([this, lastReplayedRevision]() {
            recordReplayedRevision(*lastReplayedRevision);
            mMainStoreTransaction.abort();
            if (allChangesReplayed()) {
                mReplayInProgress = false;
                emit changesReplayed();
            } else {
                QTimer::singleShot(0, [this]() {
                    replayNextRevision().exec();
                });
            }
        });
}

void ChangeReplay::revisionChanged()
{
    if (!mReplayInProgress) {
        emit replayingChanges();
        replayNextRevision().exec();
    }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "moc_changereplay.cpp"
#pragma clang diagnostic pop
