#include "index.h"

#include "log.h"

SINK_DEBUG_AREA("index")

Index::Index(const QString &storageRoot, const QString &name, Sink::Storage::DataStore::AccessMode mode)
    : mTransaction(Sink::Storage::DataStore(storageRoot, name, mode).createTransaction(mode)),
      mDb(mTransaction.openDatabase(name.toLatin1(), std::function<void(const Sink::Storage::DataStore::Error &)>(), true)),
      mName(name)
{
}

Index::Index(const QByteArray &name, Sink::Storage::DataStore::Transaction &transaction)
    : mDb(transaction.openDatabase(name, std::function<void(const Sink::Storage::DataStore::Error &)>(), true)), mName(name)
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

void Index::lookup(const QByteArray &key, const std::function<void(const QByteArray &value)> &resultHandler, const std::function<void(const Error &error)> &errorHandler, bool matchSubStringKeys)
{
    mDb.scan(key,
        [this, resultHandler](const QByteArray &key, const QByteArray &value) -> bool {
            resultHandler(value);
            return true;
        },
        [this, errorHandler](const Sink::Storage::DataStore::Error &error) {
            SinkWarning() << "Error while retrieving value" << error.message;
            errorHandler(Error(error.store, error.code, error.message));
        },
        matchSubStringKeys);
}

QByteArray Index::lookup(const QByteArray &key)
{
    QByteArray result;
    //We have to create a deep copy, otherwise the returned data may become invalid when the transaction ends.
    lookup(key, [&result](const QByteArray &value) { result = QByteArray(value.constData(), value.size()); }, [this](const Index::Error &error) { SinkTrace() << "Error while retrieving value" << error.message; });
    return result;
}
