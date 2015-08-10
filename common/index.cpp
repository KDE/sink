#include "index.h"
#include <QDebug>

Index::Index(const QString &storageRoot, const QString &name, Akonadi2::Storage::AccessMode mode)
    : mStorage(storageRoot, name, mode, true)
{

}

void Index::add(const QByteArray &key, const QByteArray &value)
{
    mStorage.createTransaction(Akonadi2::Storage::ReadWrite).write(key, value);
}

void Index::lookup(const QByteArray &key, const std::function<void(const QByteArray &value)> &resultHandler,
                                          const std::function<void(const Error &error)> &errorHandler)
{
    if (!mStorage.exists()) {
        errorHandler(Error("index", IndexNotAvailable, "Index not existing"));
        return;
    }
    mStorage.createTransaction(Akonadi2::Storage::ReadOnly).scan(key, [this, resultHandler](const QByteArray &key, const QByteArray &value) -> bool {
        resultHandler(value);
        return true;
    },
    [errorHandler](const Akonadi2::Storage::Error &error) {
        qDebug() << "Error while retrieving value" << error.message;
        errorHandler(Error(error.store, error.code, error.message));
    }
    );
}

