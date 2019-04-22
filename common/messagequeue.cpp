/*
 * Copyright (C) 2019 Christian Mollekopf <mollekopf@kolabsys.com>
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
#include "messagequeue.h"
#include "storage.h"
#include "storage/key.h"
#include <log.h>

using namespace Sink::Storage;

MessageQueue::MessageQueue(const QString &storageRoot, const QString &name) : mStorage(storageRoot, name, DataStore::ReadWrite), mReplayedRevision{-1}
{
}

MessageQueue::~MessageQueue()
{
    if (mWriteTransaction) {
        mWriteTransaction.abort();
    }
}

void MessageQueue::enqueue(void const *msg, size_t size)
{
    enqueue(QByteArray::fromRawData(static_cast<const char *>(msg), size));
}

void MessageQueue::startTransaction()
{
    if (mWriteTransaction) {
        return;
    }
    processRemovals();
    mWriteTransaction = mStorage.createTransaction(DataStore::ReadWrite);
}

void MessageQueue::commit()
{
    mWriteTransaction.commit();
    mWriteTransaction = DataStore::Transaction();
    processRemovals();
    emit messageReady();
}

void MessageQueue::enqueue(const QByteArray &value)
{
    bool implicitTransaction = false;
    if (!mWriteTransaction) {
        implicitTransaction = true;
        startTransaction();
    }
    const qint64 revision = DataStore::maxRevision(mWriteTransaction) + 1;
    mWriteTransaction.openDatabase().write(Revision{size_t(revision)}.toDisplayByteArray(), value);
    Sink::Storage::DataStore::setMaxRevision(mWriteTransaction, revision);
    if (implicitTransaction) {
        commit();
    }
}

void MessageQueue::processRemovals()
{
    if (mWriteTransaction) {
        if (mReplayedRevision > 0) {
            auto dequedRevisions = mReplayedRevision - Sink::Storage::DataStore::cleanedUpRevision(mWriteTransaction);
            if (dequedRevisions > 500) {
                SinkTrace() << "We're building up a large backlog of dequeued revisions " << dequedRevisions;
            }
        }
        return;
    }
    if (mReplayedRevision >= 0) {
        auto transaction = mStorage.createTransaction(Sink::Storage::DataStore::ReadWrite);
        auto db = transaction.openDatabase();
        for (auto revision = Sink::Storage::DataStore::cleanedUpRevision(transaction) + 1; revision <= mReplayedRevision; revision++) {
            db.remove(Revision{size_t(revision)}.toDisplayByteArray());
        }
        Sink::Storage::DataStore::setCleanedUpRevision(transaction, mReplayedRevision);
        transaction.commit();
        mReplayedRevision = -1;
    }
}

void MessageQueue::dequeue(const std::function<void(void *ptr, int size, std::function<void(bool success)>)> &resultHandler, const std::function<void(const Error &error)> &errorHandler)
{
    dequeueBatch(1, [resultHandler](const QByteArray &value) {
        return KAsync::start<void>([&value, resultHandler](KAsync::Future<void> &future) {
            resultHandler(const_cast<void *>(static_cast<const void *>(value.data())), value.size(), [&future](bool success) { future.setFinished(); });
        });
    }).onError([errorHandler](const KAsync::Error &error) { errorHandler(Error("messagequeue", error.errorCode, error.errorMessage.toLatin1())); }).exec();
}

KAsync::Job<void> MessageQueue::dequeueBatch(int maxBatchSize, const std::function<KAsync::Job<void>(const QByteArray &)> &resultHandler)
{
    return KAsync::start<void>([this, maxBatchSize, resultHandler](KAsync::Future<void> &future) {
        int count = 0;
        QList<KAsync::Future<void>> waitCondition;
        mStorage.createTransaction(Sink::Storage::DataStore::ReadOnly)
            .openDatabase()
            .scan("",
                [&](const QByteArray &key, const QByteArray &value) -> bool {
                    const auto revision = key.toLongLong();
                    if (revision <= mReplayedRevision) {
                        return true;
                    }
                    mReplayedRevision = revision;

                    waitCondition << resultHandler(value).exec();

                    count++;
                    if (count < maxBatchSize) {
                        return true;
                    }
                    return false;
                },
                [](const Sink::Storage::DataStore::Error &error) {
                    SinkError() << "Error while retrieving value" << error.message;
                    // errorHandler(Error(error.store, error.code, error.message));
                });

        // Trace() << "Waiting on " << waitCondition.size() << " results";
        KAsync::waitForCompletion(waitCondition)
            .then([this, count, &future]() {
                processRemovals();
                if (count == 0) {
                    future.setFinished();
                } else {
                    if (isEmpty()) {
                        emit this->drained();
                    }
                    future.setFinished();
                }
            })
            .exec();
    });
}

bool MessageQueue::isEmpty()
{
    int count = 0;
    auto t = mStorage.createTransaction(Sink::Storage::DataStore::ReadOnly);
    auto db = t.openDatabase();
    if (db) {
        db.scan("",
            [&count, this](const QByteArray &key, const QByteArray &value) -> bool {
                const auto revision = key.toLongLong();
                if (revision <= mReplayedRevision) {
                    return true;
                }
                count++;
                return false;
            },
            [](const Sink::Storage::DataStore::Error &error) { SinkError() << "Error while checking if empty" << error.message; });
    }
    return count == 0;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "moc_messagequeue.cpp"
#pragma clang diagnostic pop
