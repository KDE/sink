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
using namespace Sink::Storage;

ChangeReplay::ChangeReplay(const ResourceContext &resourceContext, const Sink::Log::Context &ctx)
    : mStorage(storageLocation(), resourceContext.instanceId(), DataStore::ReadOnly), mChangeReplayStore(storageLocation(), resourceContext.instanceId() + ".changereplay", DataStore::ReadWrite), mReplayInProgress(false), mLogCtx{ctx.subContext("changereplay")}
{
}

qint64 ChangeReplay::getLastReplayedRevision()
{
    qint64 lastReplayedRevision = 0;
    auto replayStoreTransaction = mChangeReplayStore.createTransaction(DataStore::ReadOnly);
    replayStoreTransaction.openDatabase().scan("lastReplayedRevision",
        [&lastReplayedRevision](const QByteArray &key, const QByteArray &value) -> bool {
            lastReplayedRevision = value.toLongLong();
            return false;
        },
        [](const DataStore::Error &) {});
    return lastReplayedRevision;
}

bool ChangeReplay::allChangesReplayed()
{
    const qint64 topRevision = DataStore::maxRevision(mStorage.createTransaction(DataStore::ReadOnly, [this](const Sink::Storage::DataStore::Error &error) {
        SinkWarningCtx(mLogCtx) << error.message;
    }));
    const qint64 lastReplayedRevision = getLastReplayedRevision();
    return (lastReplayedRevision >= topRevision);
}

void ChangeReplay::recordReplayedRevision(qint64 revision)
{
    auto replayStoreTransaction = mChangeReplayStore.createTransaction(DataStore::ReadWrite, [this](const Sink::Storage::DataStore::Error &error) {
        SinkWarningCtx(mLogCtx) << error.message;
    });
    replayStoreTransaction.openDatabase().write("lastReplayedRevision", QByteArray::number(revision));
    replayStoreTransaction.commit();
};

KAsync::Job<void> ChangeReplay::replayNextRevision()
{
    Q_ASSERT(!mReplayInProgress);
    return KAsync::start<void>([this]() {
            if (mReplayInProgress) {
                SinkErrorCtx(mLogCtx) << "Replay still in progress!!!!!";
                return KAsync::null<void>();
            }
            auto lastReplayedRevision = QSharedPointer<qint64>::create(0);
            auto topRevision = QSharedPointer<qint64>::create(0);
            emit replayingChanges();
            mReplayInProgress = true;
            mMainStoreTransaction = mStorage.createTransaction(DataStore::ReadOnly, [this](const DataStore::Error &error) {
                SinkWarningCtx(mLogCtx) << error.message;
            });
            auto replayStoreTransaction = mChangeReplayStore.createTransaction(DataStore::ReadOnly, [this](const DataStore::Error &error) {
                SinkWarningCtx(mLogCtx) << error.message;
            });
            Q_ASSERT(mMainStoreTransaction);
            Q_ASSERT(replayStoreTransaction);
            replayStoreTransaction.openDatabase().scan("lastReplayedRevision",
                [lastReplayedRevision](const QByteArray &key, const QByteArray &value) -> bool {
                    *lastReplayedRevision = value.toLongLong();
                    return false;
                },
                [](const DataStore::Error &) {});
            *topRevision = DataStore::maxRevision(mMainStoreTransaction);
            if (*lastReplayedRevision >= *topRevision) {
                SinkTraceCtx(mLogCtx) << "Nothing to replay";
                return KAsync::null();
            }
            SinkTraceCtx(mLogCtx) << "Changereplay from " << *lastReplayedRevision << " to " << *topRevision;
            return KAsync::doWhile(
                [this, lastReplayedRevision, topRevision]() -> KAsync::Job<KAsync::ControlFlowFlag> {
                    if (*lastReplayedRevision >= *topRevision) {
                        SinkTraceCtx(mLogCtx) << "Done replaying" << *lastReplayedRevision << *topRevision;
                        return KAsync::value(KAsync::Break);
                    }
                    Q_ASSERT(mMainStoreTransaction);

                    auto replayJob = KAsync::null();
                    qint64 revision = *lastReplayedRevision + 1;
                    while (revision <= *topRevision) {
                        const auto uid = DataStore::getUidFromRevision(mMainStoreTransaction, revision);
                        const auto type = DataStore::getTypeFromRevision(mMainStoreTransaction, revision);
                        if (uid.isEmpty() || type.isEmpty()) {
                            SinkErrorCtx(mLogCtx) << "Failed to read uid or type for revison: " << revision << uid << type;
                        } else {
                            const auto key = DataStore::assembleKey(uid, revision);
                            QByteArray entityBuffer;
                            DataStore::mainDatabase(mMainStoreTransaction, type)
                                .scan(key,
                                    [&entityBuffer](const QByteArray &key, const QByteArray &value) -> bool {
                                        entityBuffer = value;
                                        return false;
                                    },
                                    [this, key](const DataStore::Error &) { SinkErrorCtx(mLogCtx) << "Failed to read the entity buffer " << key; });

                            if (entityBuffer.isEmpty()) {
                                SinkErrorCtx(mLogCtx) << "Failed to replay change " << key;
                            } else {
                                if (canReplay(type, key, entityBuffer)) {
                                    SinkTraceCtx(mLogCtx) << "Replaying " << key;
                                    replayJob = replay(type, key, entityBuffer);
                                    //Set the last revision we tried to replay
                                    *lastReplayedRevision = revision;
                                    //Execute replay job and commit
                                    break;
                                } else {
                                    SinkTraceCtx(mLogCtx) << "Not replaying " << key;
                                    //We silently skip over revisions that cannot be replayed, as this is not an error.
                                }
                            }
                        }
                        //Bump the revision if we failed to even attempt to replay. This will simply skip over those revisions, as we can't recover from those situations.
                        *lastReplayedRevision = revision;
                        revision++;
                    }
                    return replayJob.then([=](const KAsync::Error &error) {
                        if (error) {
                            SinkWarningCtx(mLogCtx) << "Change replay failed: " << error  << "Last replayed revision: "  << *lastReplayedRevision;
                            //We're probably not online or so, so postpone retrying
                            return KAsync::value(KAsync::Break).then(KAsync::error<KAsync::ControlFlowFlag>(error));
                        }
                        SinkTraceCtx(mLogCtx) << "Replayed until: " << *lastReplayedRevision;

                        recordReplayedRevision(*lastReplayedRevision);
                        reportProgress(*lastReplayedRevision, *topRevision);

                        const bool gotMoreToReplay = (*lastReplayedRevision < *topRevision);
                        if (gotMoreToReplay) {
                            SinkTraceCtx(mLogCtx) << "Replaying some more...";
                            //Replay more if we have more
                            return KAsync::wait(0).then(KAsync::value(KAsync::Continue));
                        } else {
                            return KAsync::value(KAsync::Break);
                        }
                    }).guard(&mGuard);
            });
        })
        .then([this](const KAsync::Error &error) {
            SinkTraceCtx(mLogCtx) << "Change replay complete.";
            if (error) {
                SinkWarningCtx(mLogCtx) << "Error during change replay: " << error;
            }
            mMainStoreTransaction.abort();
            mReplayInProgress = false;
            if (ChangeReplay::allChangesReplayed()) {
                //In case we have a derived implementation
                if (allChangesReplayed()) {
                    SinkTraceCtx(mLogCtx) << "All changes replayed";
                    emit changesReplayed();
                }
            }
            if (error) {
                return KAsync::error(error);
            } else {
                return KAsync::null();
            }
        }).guard(&mGuard);
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
