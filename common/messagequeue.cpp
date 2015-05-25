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
    mStorage.startTransaction(Akonadi2::Storage::ReadWrite);
    const qint64 revision = mStorage.maxRevision() + 1;
    const QByteArray key = QString("%1").arg(revision).toUtf8();
    mStorage.write(key.data(), key.size(), msg, size);
    mStorage.setMaxRevision(revision);
    mStorage.commitTransaction();
    emit messageReady();
}

void MessageQueue::dequeue(const std::function<void(void *ptr, int size, std::function<void(bool success)>)> &resultHandler,
                           const std::function<void(const Error &error)> &errorHandler)
{
    bool readValue = false;
    mStorage.scan("", [this, resultHandler, errorHandler, &readValue](void *keyPtr, int keySize, void *valuePtr, int valueSize) -> bool {
        //We need a copy of the key here, otherwise we can't store it in the lambda (the pointers will become invalid)
        const auto key  = QByteArray(static_cast<char*>(keyPtr), keySize);
        if (Akonadi2::Storage::isInternalKey(key)) {
            return true;
        }
        readValue = true;
        resultHandler(valuePtr, valueSize, [this, key, errorHandler](bool success) {
            if (success) {
                mStorage.remove(key.data(), key.size(), [errorHandler, key](const Akonadi2::Storage::Error &error) {
                    ErrorMsg() << "Error while removing value" << error.message << key;
                    //Don't call the errorhandler in here, we already called the result handler
                });
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
    mStorage.scan("", [&count](void *keyPtr, int keySize, void *valuePtr, int valueSize) -> bool {
        const auto key = QByteArray::fromRawData(static_cast<char*>(keyPtr), keySize);
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

