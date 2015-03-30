#include "index.h"
#include <QDebug>

Index::Index(const QString &storageRoot, const QString &name, Akonadi2::Storage::AccessMode mode)
    : mStorage(storageRoot, name, mode, true)
{

}

void Index::add(const QByteArray &key, const QByteArray &value)
{
    mStorage.startTransaction(Akonadi2::Storage::ReadWrite);
    mStorage.write(key.data(), key.size(), value.data(), value.size());
    mStorage.commitTransaction();
}

void Index::lookup(const QByteArray &key, const std::function<void(const QByteArray &value)> &resultHandler,
                                          const std::function<void(const Error &error)> &errorHandler)
{
    mStorage.scan(key, [this, resultHandler](void *keyPtr, int keySize, void *valuePtr, int valueSize) -> bool {
        resultHandler(QByteArray(static_cast<char*>(valuePtr), valueSize));
        return true;
    },
    [errorHandler](const Akonadi2::Storage::Error &error) {
        qDebug() << "Error while retrieving value" << error.message;
        errorHandler(Error(error.store, error.code, error.message));
    }
    );
}

