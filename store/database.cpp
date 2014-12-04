#include "database.h"

#include <iostream>
#include <QDir>
#include <QString>
#include <QTime>
#include <qdebug.h>

Database::Database(const QString &path)
{
    int rc;

    QDir dir;
    dir.mkdir(path);

    //create file
    rc = mdb_env_create(&env);
    rc = mdb_env_open(env, path.toStdString().data(), 0, 0664);
    const int dbSize = 10485760*100; //10MB * 100
    mdb_env_set_mapsize(env, dbSize);

    if (rc) {
        std::cerr << "mdb_env_open: " << rc << mdb_strerror(rc) << std::endl;
    }
}

Database::~Database()
{
    mdb_close(env, dbi);
    mdb_env_close(env);
}

MDB_txn *Database::startTransaction()
{
    int rc;
    MDB_txn *transaction;
    rc = mdb_txn_begin(env, NULL, 0, &transaction);
    rc = mdb_open(transaction, NULL, 0, &dbi);
    return transaction;
}

void Database::endTransaction(MDB_txn *transaction)
{
    int rc;
    rc = mdb_txn_commit(transaction);
    if (rc) {
        std::cerr << "mdb_txn_commit: " << rc << mdb_strerror(rc) << std::endl;
    }
}


void Database::write(const std::string &sKey, const std::string &sValue, MDB_txn *transaction)
{
    int rc;
    MDB_val key, data;
    key.mv_size = sKey.size();
    key.mv_data = (void*)sKey.data();
    data.mv_size = sValue.size();
    data.mv_data = (void*)sValue.data();
    rc = mdb_put(transaction, dbi, &key, &data, 0);
    if (rc) {
        std::cerr << "mdb_put: " << rc << mdb_strerror(rc) << std::endl;
    }
}

void Database::read(const std::string &sKey, const std::function<void(const std::string)> &resultHandler)
{
    int rc;
    MDB_txn *txn;
    MDB_val key;
    MDB_val data;
    MDB_cursor *cursor;

    key.mv_size = sKey.size();
    key.mv_data = (void*)sKey.data();

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (sKey.empty()) {
        std::cout << "Iterating over all values of store!" << std::endl;
        while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
            const std::string resultKey(static_cast<char*>(key.mv_data), key.mv_size);
            const std::string resultValue(static_cast<char*>(data.mv_data), data.mv_size);
            // std::cout << "key: " << resultKey << " data: " << resultValue << std::endl;
            resultHandler(resultValue);
        }
    } else {
        if ((rc = mdb_cursor_get(cursor, &key, &data, MDB_SET)) == 0) {
            const std::string resultKey(static_cast<char*>(key.mv_data), key.mv_size);
            const std::string resultValue(static_cast<char*>(data.mv_data), data.mv_size);
            // std::cout << "key: " << resultKey << " data: " << resultValue << std::endl;
            resultHandler(resultValue);
        } else {
            std::cout << "couldn't find value " << sKey << std::endl;
        }
    }
    if (rc) {
        std::cerr << "mdb_cursor_get: " << rc << mdb_strerror(rc) << std::endl;
    }
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
}



ReadTransaction::ReadTransaction(const QString &path)
{
    int rc;

    //create file
    rc = mdb_env_create(&env);
    //FIXME MDB_NOTLS required to so we can have multiple read-only transactions per resource?
    rc = mdb_env_open(env, path.toStdString().data(), 0, 0664);
    const int dbSize = 10485760*100; //10MB * 100
    mdb_env_set_mapsize(env, dbSize);

    if (rc) {
        std::cerr << "mdb_env_open: " << rc << mdb_strerror(rc) << std::endl;
    }
}

ReadTransaction::~ReadTransaction()
{
    mdb_txn_abort(txn);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

void ReadTransaction::read(const std::string &sKey, const std::function<void(void *data, int size)> &resultHandler)
{
    int rc;
    MDB_val key;
    MDB_val data;
    MDB_cursor *cursor;

    key.mv_size = sKey.size();
    key.mv_data = (void*)sKey.data();

    {
        //A write transaction is at least required the first time
        rc = mdb_txn_begin(env, nullptr, 0, &txn);
        //Open the database
        //With this we could open multiple named databases if we wanted to
        rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
        mdb_txn_abort(txn);
    }

    rc = mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn);
    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc) {
        std::cerr << "mdb_cursor_open: " << rc << mdb_strerror(rc) << std::endl;
    }
    if (sKey.empty()) {
        std::cout << "Iterating over all values of store!" << std::endl;
        rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
        while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
            resultHandler(data.mv_data, data.mv_size);
        }
        //We never find the last value
        if (rc == MDB_NOTFOUND) {
            rc = 0;
        }
    } else {
        if ((rc = mdb_cursor_get(cursor, &key, &data, MDB_SET)) == 0) {
            resultHandler(data.mv_data, data.mv_size);
        } else {
            std::cout << "couldn't find value " << sKey << std::endl;
        }
    }
    if (rc) {
        std::cerr << "mdb_cursor_get: " << rc << mdb_strerror(rc) << std::endl;
    }
    mdb_cursor_close(cursor);
    //We keep the transaction open since we want to keep the returned values alive
}

