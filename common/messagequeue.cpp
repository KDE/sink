#include "messagequeue.h"
#include <QDebug>

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
    mStorage.scan("", 0, [this, resultHandler, &readValue](void *keyPtr, int keySize, void *valuePtr, int valueSize) -> bool {
        const auto key  = QByteArray::fromRawData(static_cast<char*>(keyPtr), keySize);
        if (key.startsWith("__internal")) {
            return true;
        }
        readValue = true;
        resultHandler(valuePtr, valueSize, [this, key](bool success) {
            if (success) {
                mStorage.remove(key.data(), key.size());
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
        qDebug() << "Error while retrieving value" << QString::fromStdString(error.message);
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
        if (!key.startsWith("__internal")) {
            count++;
            return false;
        }
        return true;
    });
    return count == 0;
}

