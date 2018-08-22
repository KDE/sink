#include "index.h"

#include "log.h"

using Sink::Storage::Identifier;

Index::Index(const QString &storageRoot, const QString &dbName, const QString &indexName, Sink::Storage::DataStore::AccessMode mode)
    : mTransaction(Sink::Storage::DataStore(storageRoot, dbName, mode).createTransaction(mode)),
      mDb(mTransaction.openDatabase(indexName.toLatin1(), std::function<void(const Sink::Storage::DataStore::Error &)>(), Sink::Storage::AllowDuplicates)),
      mName(indexName),
      mLogCtx("index." + indexName.toLatin1())
{
}

Index::Index(const QString &storageRoot, const QString &name, Sink::Storage::DataStore::AccessMode mode)
    : mTransaction(Sink::Storage::DataStore(storageRoot, name, mode).createTransaction(mode)),
      mDb(mTransaction.openDatabase(name.toLatin1(), std::function<void(const Sink::Storage::DataStore::Error &)>(), Sink::Storage::AllowDuplicates)),
      mName(name),
      mLogCtx("index." + name.toLatin1())
{
}

Index::Index(const QString &storageRoot, const Sink::Storage::DbLayout &layout, Sink::Storage::DataStore::AccessMode mode)
    : mTransaction(Sink::Storage::DataStore(storageRoot, layout, mode).createTransaction(mode)),
      mDb(mTransaction.openDatabase(layout.name, std::function<void(const Sink::Storage::DataStore::Error &)>(), Sink::Storage::AllowDuplicates)),
      mName(layout.name),
      mLogCtx("index." + layout.name)
{
}

Index::Index(const QByteArray &name, Sink::Storage::DataStore::Transaction &transaction)
    : mDb(transaction.openDatabase(name, std::function<void(const Sink::Storage::DataStore::Error &)>(), Sink::Storage::AllowDuplicates)), mName(name),
      mLogCtx("index." + name)
{
}

void Index::add(const Identifier &key, const QByteArray &value)
{
    add(key.toInternalByteArray(), value);
}

void Index::add(const QByteArray &key, const QByteArray &value)
{
    Q_ASSERT(!key.isEmpty());
    mDb.write(key, value, [&] (const Sink::Storage::DataStore::Error &error) {
        SinkWarningCtx(mLogCtx) << "Error while writing value" << error;
    });
}

void Index::remove(const Identifier &key, const QByteArray &value)
{
    remove(key.toInternalByteArray(), value);
}

void Index::remove(const QByteArray &key, const QByteArray &value)
{
    mDb.remove(key, value, [&] (const Sink::Storage::DataStore::Error &error) {
        SinkWarningCtx(mLogCtx) << "Error while removing value: " << key << value << error;
    });
}

void Index::lookup(const QByteArray &key, const std::function<void(const QByteArray &value)> &resultHandler, const std::function<void(const Error &error)> &errorHandler, bool matchSubStringKeys)
{
    mDb.scan(key,
        [&](const QByteArray &key, const QByteArray &value) -> bool {
            resultHandler(value);
            return true;
        },
        [&](const Sink::Storage::DataStore::Error &error) {
            SinkWarningCtx(mLogCtx) << "Error while retrieving value:" << error << mName;
            errorHandler(Error(error.store, error.code, error.message));
        },
        matchSubStringKeys);
}

QByteArray Index::lookup(const QByteArray &key)
{
    QByteArray result;
    //We have to create a deep copy, otherwise the returned data may become invalid when the transaction ends.
    lookup(key, [&](const QByteArray &value) { result = QByteArray(value.constData(), value.size()); }, [](const Index::Error &) { });
    return result;
}

void Index::rangeLookup(const QByteArray &lowerBound, const QByteArray &upperBound,
        const std::function<void(const QByteArray &value)> &resultHandler,
        const std::function<void(const Error &error)> &errorHandler)
{
    mDb.findAllInRange(lowerBound, upperBound,
            [&](const QByteArray &key, const QByteArray &value) {
                resultHandler(value);
            },
            [&](const Sink::Storage::DataStore::Error &error) {
                SinkWarningCtx(mLogCtx) << "Error while retrieving value:" << error << mName;
                errorHandler(Error(error.store, error.code, error.message));
            });
}
