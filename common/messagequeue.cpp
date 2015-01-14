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
    mStorage.scan("", 0, [this, resultHandler](void *keyPtr, int keySize, void *valuePtr, int valueSize) -> bool {
        const std::string key(static_cast<char*>(keyPtr), keySize);
        resultHandler(valuePtr, valueSize, [this, key](bool success) {
            if (success) {
                mStorage.remove(key.data(), key.size());
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
}

bool MessageQueue::isEmpty()
{
    int count = 0;
    mStorage.scan("", [&count](void *keyPtr, int keySize, void *valuePtr, int valueSize) -> bool {
        const QByteArray key(static_cast<char*>(keyPtr), keySize);
        if (!key.startsWith("__internal")) {
            count++;
        }
    });
    return count == 0;
}

