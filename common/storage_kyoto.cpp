#include "storage.h"

#include <iostream>

#include <QAtomicInt>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QReadWriteLock>
#include <QString>
#include <QTime>

#include <kchashdb.h>

class Storage::Private
{
public:
    Private(const QString &storageRoot, const QString &name, AccessMode m);
    ~Private();

    kyotocabinet::TreeDB db;
    AccessMode mode;
    bool dbOpen;
    bool inTransaction;
};

Storage::Private::Private(const QString &storageRoot, const QString &name, AccessMode m)
    : mode(m),
      dbOpen(false),
      inTransaction(false)
{
    QDir dir;
    dir.mkdir(storageRoot);

    //create file
    uint32_t openMode = kyotocabinet::BasicDB::OCREATE |
                       (mode == ReadOnly ? kyotocabinet::BasicDB::OREADER
                                         : kyotocabinet::BasicDB::OWRITER);
    dbOpen = db.open((storageRoot + "/" + name + ".kch").toStdString(), openMode);
    if (!dbOpen) {
        std::cerr << "Could not open database: " << db.error().codename(db.error().code())  << " " << db.error().message() << std::endl;
        // TODO: handle error
    }
}

Storage::Private::~Private()
{
    if (dbOpen && inTransaction) {
        db.end_transaction(false);
    }
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
    return d->inTransaction;
}

bool Storage::startTransaction(AccessMode type)
{
    if (!d->dbOpen) {
        return false;
    }

    if (type == ReadWrite && d->mode != ReadWrite) {
        return false;
    }

    if (d->inTransaction) {
        return true;
    }

    //TODO handle errors
    d->inTransaction = d->db.begin_transaction();
    return d->inTransaction;
}

bool Storage::commitTransaction()
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

void Storage::abortTransaction()
{
    if (!d->dbOpen || !d->inTransaction) {
        return;
    }

    d->db.end_transaction(false);
    d->inTransaction = false;
}

bool Storage::write(const char *key, size_t keySize, const char *value, size_t valueSize)
{
    if (!d->dbOpen) {
        return false;
    }

    bool success = d->db.set(key, keySize, value, valueSize);
    return success; 
}

bool Storage::write(const std::string &sKey, const std::string &sValue)
{
    if (!d->dbOpen) {
        return false;
    }

    bool success = d->db.set(sKey, sValue);
    return success; 
}

bool Storage::read(const std::string &sKey, const std::function<void(const std::string &value)> &resultHandler)
{
    if (!d->dbOpen) {
        return false;
    }

    std::string value;
    if (d->db.get(sKey, &value)) {
        resultHandler(value);
        return true;
    }

    return false;
}

bool Storage::read(const std::string &sKey, const std::function<void(void *ptr, int size)> &resultHandler)
{
    if (!d->dbOpen) {
        return false;
    }

    size_t valueSize;
    char *valueBuffer = d->db.get(sKey.data(), sKey.size(), &valueSize);
    if (valueBuffer) {
        resultHandler(valueBuffer, valueSize);
    }
    delete[] valueBuffer;
    return valueBuffer != nullptr;
}

qint64 Storage::diskUsage() const
{
    if (!d->dbOpen) {
        return 0;
    }

    QFileInfo info(QString::fromStdString(d->db.path()));
    return info.size();
}

void Storage::removeFromDisk() const
{
    if (!d->dbOpen) {
        return;
    }

   QFileInfo info(QString::fromStdString(d->db.path()));
   QDir dir = info.dir();
   dir.remove(info.fileName());
}
