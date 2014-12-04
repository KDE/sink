#include "database.h"

#include <iostream>

#include <QAtomicInt>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QReadWriteLock>
#include <QString>
#include <QTime>

#include <kchashdb.h>

class Database::Private
{
public:
    Private(const QString &storageRoot, const QString &name);
    ~Private();

    kyotocabinet::TreeDB db;
    bool dbOpen;
    bool inTransaction;
};

Database::Private::Private(const QString &storageRoot, const QString &name)
    : inTransaction(false)
{
    QDir dir;
    dir.mkdir(storageRoot);

    //create file
    dbOpen = db.open((storageRoot + "/" + name + ".kch").toStdString(), kyotocabinet::BasicDB::OWRITER | kyotocabinet::BasicDB::OCREATE);
    if (!dbOpen) {
        // TODO: handle error
    }
}

Database::Private::~Private()
{
    if (dbOpen && inTransaction) {
        db.end_transaction(false);
    }
}

Database::Database(const QString &storageRoot, const QString &name)
    : d(new Private(storageRoot, name))
{
}

Database::~Database()
{
    delete d;
}

bool Database::isInTransaction() const
{
    return d->inTransaction;
}

bool Database::startTransaction(TransactionType type)
{
    if (!d->dbOpen) {
        return false;
    }

    if (d->inTransaction) {
        return true;
    }

    //TODO handle errors
    d->inTransaction = d->db.begin_transaction();
    return d->inTransaction;
}

bool Database::commitTransaction()
{
    if (!d->dbOpen) {
        return false;
    }

    if (!d->inTransaction) {
        return false;
    }

    bool success = d->db.end_transaction(true);
    d->inTransaction = false;
    return success;
}

void Database::abortTransaction()
{
    if (!d->dbOpen || !d->inTransaction) {
        return;
    }

    d->db.end_transaction(false);
    d->inTransaction = false;
}

bool Database::write(const char *key, size_t keySize, const char *value, size_t valueSize)
{
    if (!d->dbOpen) {
        return false;
    }

    bool success = d->db.set(key, keySize, value, valueSize);
    return success; 
}

bool Database::write(const std::string &sKey, const std::string &sValue)
{
    if (!d->dbOpen) {
        return false;
    }

    bool success = d->db.set(sKey, sValue);
    return success; 
}

void Database::read(const std::string &sKey, const std::function<void(const std::string &value)> &resultHandler)
{
    if (!d->dbOpen) {
        return;
    }

    std::string value;
    if (d->db.get(sKey, &value)) {
        resultHandler(value);
    }
}

void Database::read(const std::string &sKey, const std::function<void(void *ptr, int size)> &resultHandler)
{
    if (!d->dbOpen) {
        return;
    }

    size_t valueSize;
    char *valueBuffer = d->db.get(sKey.data(), sKey.size(), &valueSize);
    resultHandler(valueBuffer, valueSize);
    delete[] valueBuffer;
}

qint64 Database::diskUsage() const
{
    if (!d->dbOpen) {
        return 0;
    }

    QFileInfo info(QString::fromStdString(d->db.path()));
    return info.size();
}

void Database::removeFromDisk() const
{
    if (!d->dbOpen) {
        return;
    }

   QFileInfo info(QString::fromStdString(d->db.path()));
   QDir dir = info.dir();
   dir.remove(info.fileName());
}
