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
    auto transaction = mStorage.createTransaction(Akonadi2::Storage::ReadWrite);
    const qint64 revision = mStorage.maxRevision() + 1;
    const QByteArray key = QString("%1").arg(revision).toUtf8();
    transaction.write(key, QByteArray::fromRawData(static_cast<const char*>(msg), size));
    Akonadi2::Storage::setMaxRevision(transaction, revision);
    emit messageReady();
}

void MessageQueue::dequeue(const std::function<void(void *ptr, int size, std::function<void(bool success)>)> &resultHandler,
                           const std::function<void(const Error &error)> &errorHandler)
{
    bool readValue = false;
    mTransaction = std::move(mStorage.createTransaction(Akonadi2::Storage::ReadWrite));
    mTransaction.scan("", [this, resultHandler, errorHandler, &readValue](const QByteArray &key, const QByteArray &value) -> bool {
        if (Akonadi2::Storage::isInternalKey(key)) {
            return true;
        }
        readValue = true;
        //We need a copy of the key here, otherwise we can't store it in the lambda (the pointers will become invalid)
        const auto keyCopy  = QByteArray(key.constData(), key.size());
        resultHandler(const_cast<void*>(static_cast<const void*>(value.data())), value.size(), [this, keyCopy, errorHandler](bool success) {
            if (success) {
                mTransaction.remove(keyCopy, [errorHandler, keyCopy](const Akonadi2::Storage::Error &error) {
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

