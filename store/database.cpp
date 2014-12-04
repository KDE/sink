#include "database.h"

#include <iostream>

#include <QAtomicInt>
#include <QDebug>
#include <QDir>
#include <QReadWriteLock>
#include <QString>
#include <QTime>

#include <lmdb.h>

class Database::Private
{
public:
    Private(const QString &path);
    ~Private();

    MDB_dbi dbi;
    MDB_env *env;
    MDB_txn *transaction;
    bool readTransaction;
    bool firstOpen;
};

Database::Private::Private(const QString &path)
    : transaction(0),
      readTransaction(false),
      firstOpen(true)
{
    QDir dir;
    dir.mkdir(path);

    //create file
    if (mdb_env_create(&env)) {
        // TODO: handle error
    } else {
        int rc = mdb_env_open(env, path.toStdString().data(), 0, 0664);

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

Database::Private::~Private()
{
    if (transaction) {
        mdb_txn_abort(transaction);
    }

    // it is still there and still unused, so we can shut it down
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

Database::Database(const QString &path)
    : d(new Private(path))
{
}

Database::~Database()
{
    delete d;
}

bool Database::isInTransaction() const
{
    return d->transaction;
}

bool Database::startTransaction(TransactionType type)
{
    if (!d->env) {
        return false;
    }

    bool requestedRead = type == ReadOnly;
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

bool Database::commitTransaction()
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

void Database::abortTransaction()
{
    if (!d->env || !d->transaction) {
        return;
    }

    mdb_txn_abort(d->transaction);
    d->transaction = 0;
}

bool Database::write(const std::string &sKey, const std::string &sValue)
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

void Database::read(const std::string &sKey, const std::function<void(const std::string &value)> &resultHandler)
{
    read(sKey,
         [&](void *ptr, int size) {
            const std::string resultValue(static_cast<char*>(ptr), size);
            resultHandler(resultValue);
         });
// std::cout << "key: " << resultKey << " data: " << resultValue << std::endl;
}

void Database::read(const std::string &sKey, const std::function<void(void *ptr, int size)> &resultHandler)
{
    if (!d->env) {
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
            return;
        }
    }

    rc = mdb_cursor_open(d->transaction, d->dbi, &cursor);
    if (rc) {
        std::cerr << "mdb_cursor_get: " << rc << " " << mdb_strerror(rc) << std::endl;
        return;
    }

    if (sKey.empty()) {
        std::cout << "Iterating over all values of store!" << std::endl;
        rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
        while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
            resultHandler(key.mv_data, data.mv_size);
        }

        //We never find the last value
        if (rc == MDB_NOTFOUND) {
            rc = 0;
        }
    } else {
        if ((rc = mdb_cursor_get(cursor, &key, &data, MDB_SET)) == 0) {
            resultHandler(data.mv_data, data.mv_size);
        } else {
            std::cout << "couldn't find value " << sKey << " " << std::endl;
        }
    }

    if (rc) {
        std::cerr << "mdb_cursor_get: " << rc << " " << mdb_strerror(rc) << std::endl;
    }

    mdb_cursor_close(cursor);

    /**
      we don't abort the transaction since we need it for reading the values
    if (implicitTransaction) {
        abortTransaction();
    }
    */
}

