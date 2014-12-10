#include "storage.h"

#include <iostream>

#include <QAtomicInt>
#include <QDebug>
#include <QDir>
#include <QReadWriteLock>
#include <QString>
#include <QTime>

#include <lmdb.h>

class Storage::Private
{
public:
    Private(const QString &s, const QString &name, AccessMode m);
    ~Private();

    QString storageRoot;
    QString name;

    MDB_dbi dbi;
    MDB_env *env;
    MDB_txn *transaction;
    AccessMode mode;
    bool readTransaction;
    bool firstOpen;
};

Storage::Private::Private(const QString &s, const QString &n, AccessMode m)
    : storageRoot(s),
      name(n),
      transaction(0),
      mode(m),
      readTransaction(false),
      firstOpen(true)
{
    QDir dir;
    dir.mkdir(storageRoot);

    //create file
    if (mdb_env_create(&env)) {
        // TODO: handle error
    } else {
        int rc = mdb_env_open(env, storageRoot.toStdString().data(), 0, 0664);

        if (rc) {
            std::cerr << "mdb_env_open: " << rc << " " << mdb_strerror(rc) << std::endl;
            mdb_env_close(env);
            env = 0;
        } else {
            const size_t dbSize = 10485760 * 100; //10MB * 100
            mdb_env_set_mapsize(env, dbSize);
        }
    }
}

Storage::Private::~Private()
{
    if (transaction) {
        mdb_txn_abort(transaction);
    }

    // it is still there and still unused, so we can shut it down
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

Storage::Storage(const QString &storageRoot, const QString &name, AccessMode mode)
    : d(new Private(storageRoot, name, mode))
{
}

Storage::~Storage()
{
    delete d;
}

bool Storage::isInTransaction() const
{
    return d->transaction;
}

bool Storage::startTransaction(AccessMode type)
{
    if (!d->env) {
        return false;
    }

    bool requestedRead = type == ReadOnly;

    if (d->mode == ReadOnly && !requestedRead) {
        return false;
    }

    if (d->transaction && (!d->readTransaction || requestedRead)) {
        return true;
    }

    if (d->transaction) {
        // we are about to turn a read transaction into a writable one
        abortTransaction();
    }

    if (d->firstOpen && requestedRead) {
        //A write transaction is at least required the first time
        mdb_txn_begin(d->env, nullptr, 0, &d->transaction);
        //Open the database
        //With this we could open multiple named databases if we wanted to
        mdb_dbi_open(d->transaction, nullptr, 0, &d->dbi);
        mdb_txn_abort(d->transaction);
    }

    int rc;
    rc = mdb_txn_begin(d->env, NULL, requestedRead ? MDB_RDONLY : 0, &d->transaction);
    if (!rc) {
        rc = mdb_dbi_open(d->transaction, NULL, 0, &d->dbi);
    }

    d->firstOpen = false;
    return !rc;
}

bool Storage::commitTransaction()
{
    if (!d->env) {
        return false;
    }

    if (!d->transaction) {
        return false;
    }

    int rc;
    rc = mdb_txn_commit(d->transaction);
    d->transaction = 0;

    if (rc) {
        std::cerr << "mdb_txn_commit: " << rc << " " << mdb_strerror(rc) << std::endl;
    }

    return !rc;
}

void Storage::abortTransaction()
{
    if (!d->env || !d->transaction) {
        return;
    }

    mdb_txn_abort(d->transaction);
    d->transaction = 0;
}

bool Storage::write(const char *key, size_t keySize, const char *value, size_t valueSize)
{
    return write(std::string(key, keySize), std::string(value, valueSize));
}

bool Storage::write(const std::string &sKey, const std::string &sValue)
{
    if (!d->env) {
        return false;
    }

    const bool implicitTransaction = !d->transaction || d->readTransaction;
    if (implicitTransaction) {
        // TODO: if this fails, still try the write below?
        if (!startTransaction()) {
            return false;
        }
    }

    int rc;
    MDB_val key, data;
    key.mv_size = sKey.size();
    key.mv_data = (void*)sKey.data();
    data.mv_size = sValue.size();
    data.mv_data = (void*)sValue.data();
    rc = mdb_put(d->transaction, d->dbi, &key, &data, 0);

    if (rc) {
        std::cerr << "mdb_put: " << rc << " " << mdb_strerror(rc) << std::endl;
    }

    if (implicitTransaction) {
        if (rc) {
            abortTransaction();
        } else {
            rc = commitTransaction();
        }
    }

    return !rc;
}

void Storage::read(const std::string &sKey,
                   const std::function<bool(const std::string &value)> &resultHandler,
                   const std::function<void(const Storage::Error &error)> &errorHandler)
{
    read(sKey,
         [&](void *ptr, int size) -> bool {
            const std::string resultValue(static_cast<char*>(ptr), size);
            return resultHandler(resultValue);
         }, errorHandler);
// std::cout << "key: " << resultKey << " data: " << resultValue << std::endl;
}

void Storage::read(const std::string &sKey,
                   const std::function<bool(void *ptr, int size)> &resultHandler,
                   const std::function<void(const Storage::Error &error)> &errorHandler)
{
    scan(sKey, [resultHandler](void *keyPtr, int keySize, void *valuePtr, int valueSize) {
        return resultHandler(valuePtr, valueSize);
    }, errorHandler);
}

void Storage::scan(const std::string &sKey,
                   const std::function<bool(void *keyPtr, int keySize, void *valuePtr, int valueSize)> &resultHandler,
                   const std::function<void(const Storage::Error &error)> &errorHandler)
{
    if (!d->env) {
        Error error(d->name.toStdString(), -1, "Not open");
        errorHandler(error);
        return;
    }

    int rc;
    MDB_val key;
    MDB_val data;
    MDB_cursor *cursor;

    key.mv_size = sKey.size();
    key.mv_data = (void*)sKey.data();

    const bool implicitTransaction = !d->transaction;
    if (implicitTransaction) {
        // TODO: if this fails, still try the write below?
        if (!startTransaction(ReadOnly)) {
            Error error(d->name.toStdString(), -2, "Could not start transaction");
            errorHandler(error);
            return;
        }
    }

    rc = mdb_cursor_open(d->transaction, d->dbi, &cursor);
    if (rc) {
        Error error(d->name.toStdString(), rc, mdb_strerror(rc));
        errorHandler(error);
        return;
    }

    if (sKey.empty()) {
        rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
        while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
            if (!resultHandler(key.mv_data, key.mv_size, data.mv_data, data.mv_size)) {
                break;
            }
        }

        //We never find the last value
        if (rc == MDB_NOTFOUND) {
            rc = 0;
        }
    } else {
        if ((rc = mdb_cursor_get(cursor, &key, &data, MDB_SET)) == 0) {
            resultHandler(key.mv_data, key.mv_size, data.mv_data, data.mv_size);
        } else {
            std::cout << "couldn't find value " << sKey << " " << std::endl;
        }
    }

    mdb_cursor_close(cursor);

    if (rc) {
        Error error(d->name.toStdString(), rc, mdb_strerror(rc));
        errorHandler(error);
    }

    /**
      we don't abort the transaction since we need it for reading the values
    if (implicitTransaction) {
        abortTransaction();
    }
    */
}

qint64 Storage::diskUsage() const
{
    QFileInfo info(d->storageRoot + "/data.mdb");
    return info.size();
}

void Storage::removeFromDisk() const
{
    QDir dir(d->storageRoot);
    dir.remove("data.mdb");
    dir.remove("lock.mdb");
}
