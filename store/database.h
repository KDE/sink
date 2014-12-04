#pragma once

#include <lmdb.h>
#include <string>
#include <QString>

class Database {
public:
    Database(const QString &path);
    ~Database();
    MDB_txn *startTransaction();
    void endTransaction(MDB_txn *transaction);
    void write(const std::string &sKey, const std::string &sValue, MDB_txn *transaction);
    //Perhaps prefer iterators (assuming we need to be able to match multiple values
    void read(const std::string &sKey, const std::function<void(const std::string)> &);

private:
    MDB_env *env;
    MDB_dbi dbi;
};

/*
 * This opens the db for a single read transaction.
 *
 * The lifetime of all read values is tied to this transaction.
 */
class ReadTransaction {
public:
    ReadTransaction(const QString &path);
    ~ReadTransaction();

    void read(const std::string &sKey, const std::function<void(void *ptr, int size)> &);

private:
    MDB_env *env;
    MDB_dbi dbi;
    MDB_txn *txn;
};
