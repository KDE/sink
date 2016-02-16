#include "index.h"

#include "log.h"

#undef Trace
#define Trace() Trace_area("index." + mName.toLatin1())

Index::Index(const QString &storageRoot, const QString &name, Sink::Storage::AccessMode mode)
    : mTransaction(Sink::Storage(storageRoot, name, mode).createTransaction(mode)),
    mDb(mTransaction.openDatabase(name.toLatin1(), std::function<void(const Sink::Storage::Error &)>(), true)),
    mName(name)
{

}

Index::Index(const QByteArray &name, Sink::Storage::Transaction &transaction)
    : mDb(transaction.openDatabase(name, std::function<void(const Sink::Storage::Error &)>(), true)),
    mName(name)
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
                                          const std::function<void(const Error &error)> &errorHandler, bool matchSubStringKeys)
{
    mDb.scan(key, [this, resultHandler](const QByteArray &key, const QByteArray &value) -> bool {
        resultHandler(value);
        return true;
    },
    [errorHandler](const Sink::Storage::Error &error) {
        Warning() << "Error while retrieving value" << error.message;
        errorHandler(Error(error.store, error.code, error.message));
    },
    matchSubStringKeys);
}

QByteArray Index::lookup(const QByteArray &key)
{
    QByteArray result;
    lookup(key,
    [&result](const QByteArray &value) {
        result = value;
    },
    [this](const Index::Error &error) {
        Trace() << "Error while retrieving value" << error.message;
    });
    return result;
}

