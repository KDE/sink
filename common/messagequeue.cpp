#include "messagequeue.h"
#include "storage.h"
#include <QDebug>
#include <log.h>

SINK_DEBUG_AREA("messagequeue")

MessageQueue::MessageQueue(const QString &storageRoot, const QString &name) : mStorage(storageRoot, name, Sink::Storage::DataStore::ReadWrite)
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
    mWriteTransaction = mStorage.createTransaction(Sink::Storage::DataStore::ReadWrite);
}

void MessageQueue::commit()
{
    mWriteTransaction.commit();
    mWriteTransaction = Sink::Storage::DataStore::Transaction();
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
    const qint64 revision = Sink::Storage::DataStore::maxRevision(mWriteTransaction) + 1;
    const QByteArray key = QString("%1").arg(revision).toUtf8();
    mWriteTransaction.openDatabase().write(key, value);
    Sink::Storage::DataStore::setMaxRevision(mWriteTransaction, revision);
    if (implicitTransaction) {
        commit();
    }
}

void MessageQueue::processRemovals()
{
    if (mWriteTransaction) {
        return;
    }
    auto transaction = mStorage.createTransaction(Sink::Storage::DataStore::ReadWrite);
    for (const auto &key : mPendingRemoval) {
        transaction.openDatabase().remove(key);
    }
    transaction.commit();
    mPendingRemoval.clear();
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
    auto resultCount = QSharedPointer<int>::create(0);
    return KAsync::start<void>([this, maxBatchSize, resultHandler, resultCount](KAsync::Future<void> &future) {
        int count = 0;
        QList<KAsync::Future<void>> waitCondition;
        mStorage.createTransaction(Sink::Storage::DataStore::ReadOnly)
            .openDatabase()
            .scan("",
                [this, resultHandler, resultCount, &count, maxBatchSize, &waitCondition](const QByteArray &key, const QByteArray &value) -> bool {
                    if (mPendingRemoval.contains(key)) {
                        return true;
                    }
                    *resultCount += 1;
                    // We need a copy of the key here, otherwise we can't store it in the lambda (the pointers will become invalid)
                    mPendingRemoval << QByteArray(key.constData(), key.size());

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
            .syncThen<void>([this, resultCount, &future]() {
                processRemovals();
                if (*resultCount == 0) {
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
                if (!mPendingRemoval.contains(key)) {
                    count++;
                    return false;
                }
                return true;
            },
            [](const Sink::Storage::DataStore::Error &error) { SinkError() << "Error while checking if empty" << error.message; });
    }
    return count == 0;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "moc_messagequeue.cpp"
#pragma clang diagnostic pop
