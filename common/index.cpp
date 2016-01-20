#include "index.h"
#include <QDebug>

Index::Index(const QString &storageRoot, const QString &name, Sink::Storage::AccessMode mode)
    : mTransaction(Sink::Storage(storageRoot, name, mode).createTransaction(mode)),
    mDb(mTransaction.openDatabase(name.toLatin1(), std::function<void(const Sink::Storage::Error &)>(), true))
{

}

Index::Index(const QByteArray &name, Sink::Storage::Transaction &transaction)
    : mDb(transaction.openDatabase(name, std::function<void(const Sink::Storage::Error &)>(), true))
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
    [errorHandler](const Sink::Storage::Error &error) {
        qDebug() << "Error while retrieving value" << error.message;
        errorHandler(Error(error.store, error.code, error.message));
    }
    );
}

QByteArray Index::lookup(const QByteArray &key)
{
    QByteArray result;
    lookup(key,
    [&result](const QByteArray &value) {
        result = value;
    },
    [](const Index::Error &error) {
        qDebug() << "Error while retrieving value" << error.message;
    });
    return result;
}

