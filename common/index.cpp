#include "index.h"
#include <QDebug>

Index::Index(const QString &storageRoot, const QString &name, Akonadi2::Storage::AccessMode mode)
    : mTransaction(Akonadi2::Storage(storageRoot, name, mode).createTransaction(mode)),
    mDb(mTransaction.openDatabase(name.toLatin1(), std::function<void(const Akonadi2::Storage::Error &)>(), true))
{

}

Index::Index(const QByteArray &name, Akonadi2::Storage::Transaction &transaction)
    : mDb(transaction.openDatabase(name, std::function<void(const Akonadi2::Storage::Error &)>(), true))
{

}

void Index::add(const QByteArray &key, const QByteArray &value)
{
    mDb.write(key, value);
}

void Index::remove(const QByteArray &key, const QByteArray &value)
{
    mDb.remove(key, value);
}

void Index::lookup(const QByteArray &key, const std::function<void(const QByteArray &value)> &resultHandler,
                                          const std::function<void(const Error &error)> &errorHandler)
{
    mDb.scan(key, [this, resultHandler](const QByteArray &key, const QByteArray &value) -> bool {
        resultHandler(value);
        return true;
    },
    [errorHandler](const Akonadi2::Storage::Error &error) {
        qDebug() << "Error while retrieving value" << error.message;
        errorHandler(Error(error.store, error.code, error.message));
    }
    );
}

