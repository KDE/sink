#include "messagequeue.h"
#include "storage.h"
#include <QDebug>
#include <log.h>

MessageQueue::MessageQueue(const QString &storageRoot, const QString &name)
    : mStorage(storageRoot, name, Akonadi2::Storage::ReadWrite)
{

}

void MessageQueue::enqueue(void const *msg, size_t size)
{
    auto transaction = std::move(mStorage.createTransaction(Akonadi2::Storage::ReadWrite));
    const qint64 revision = Akonadi2::Storage::maxRevision(transaction) + 1;
    const QByteArray key = QString("%1").arg(revision).toUtf8();
    transaction.write(key, QByteArray::fromRawData(static_cast<const char*>(msg), size));
    Akonadi2::Storage::setMaxRevision(transaction, revision);
    transaction.commit();
    emit messageReady();
}

void MessageQueue::enqueue(const QByteArray &value)
{
    enqueue(value.data(), value.size());
}

void MessageQueue::dequeue(const std::function<void(void *ptr, int size, std::function<void(bool success)>)> &resultHandler,
                           const std::function<void(const Error &error)> &errorHandler)
{
    bool readValue = false;
    auto readTransaction = std::move(mStorage.createTransaction(Akonadi2::Storage::ReadOnly));
    readTransaction.scan("", [this, resultHandler, errorHandler, &readValue, &readTransaction](const QByteArray &key, const QByteArray &value) -> bool {
        if (Akonadi2::Storage::isInternalKey(key)) {
            return true;
        }
        readValue = true;
        //We need a copy of the key here, otherwise we can't store it in the lambda (the pointers will become invalid)
        const auto keyCopy  = QByteArray(key.constData(), key.size());
        //TODO The value copy and the early transaction abort is necessary because we don't support parallel read-transactions yet (in case of a synchronous callback)
        const auto valueCopy  = QByteArray(value.constData(), value.size());
        readTransaction.abort();
        resultHandler(const_cast<void*>(static_cast<const void*>(valueCopy.data())), valueCopy.size(), [this, keyCopy, errorHandler](bool success) {
            if (success) {
                mStorage.createTransaction(Akonadi2::Storage::ReadWrite).remove(keyCopy, [errorHandler, keyCopy](const Akonadi2::Storage::Error &error) {
                    ErrorMsg() << "Error while removing value" << error.message << keyCopy;
                    //Don't call the errorhandler in here, we already called the result handler
                });
                mTransaction.commit();
                if (isEmpty()) {
                    emit this->drained();
                }
            } else {
                //TODO re-enqueue?
            }
        });
        return false;
    },
    [errorHandler](const Akonadi2::Storage::Error &error) {
        ErrorMsg() << "Error while retrieving value" << error.message;
        errorHandler(Error(error.store, error.code, error.message));
    }
    );
    if (!readValue) {
        errorHandler(Error("messagequeue", -1, "No message found"));
    }
}

bool MessageQueue::isEmpty()
{
    int count = 0;
    mStorage.createTransaction(Akonadi2::Storage::ReadOnly).scan("", [&count](const QByteArray &key, const QByteArray &value) -> bool {
        if (!Akonadi2::Storage::isInternalKey(key)) {
            count++;
            return false;
        }
        return true;
    },
    [](const Akonadi2::Storage::Error &error) {
        qDebug() << "Error while checking if empty" << error.message;
    });
    return count == 0;
}

