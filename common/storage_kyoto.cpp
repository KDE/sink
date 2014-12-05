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

    QString name;
    kyotocabinet::TreeDB db;
    AccessMode mode;
    bool dbOpen;
    bool inTransaction;
};

Storage::Private::Private(const QString &storageRoot, const QString &n, AccessMode m)
    : name(n),
      mode(m),
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

void Storage::read(const std::string &sKey,
                   const std::function<bool(const std::string &value)> &resultHandler,
                   const std::function<void(const Storage::Error &error)> &errorHandler)
{
    if (!d->dbOpen) {
        Error error(d->name.toStdString(), -1, "Not open");
        errorHandler(error);
        return;
    }

    std::string value;
    if (sKey.empty()) {
        kyotocabinet::DB::Cursor *cursor = d->db.cursor();
        cursor->jump();

        std::string key, value;
        while (cursor->get_value(&value, true) && resultHandler(value)) {}

        delete cursor;
        return;
    } else {
        if (d->db.get(sKey, &value)) {
            resultHandler(value);
            return;
        }
    }

    Error error(d->name.toStdString(), d->db.error().code(), d->db.error().message());
    errorHandler(error);
}

void Storage::read(const std::string &sKey,
                   const std::function<bool(void *ptr, int size)> &resultHandler,
                   const std::function<void(const Storage::Error &error)> &errorHandler)
{
    if (!d->dbOpen) {
        Error error(d->name.toStdString(), -1, "Not open");
        errorHandler(error);
        return;
    }

    size_t valueSize;
    char *valueBuffer;
    if (sKey.empty()) {
        kyotocabinet::DB::Cursor *cursor = d->db.cursor();
        cursor->jump();

        while ((valueBuffer = cursor->get_value(&valueSize, true))) {
            bool ok = resultHandler(valueBuffer, valueSize);
            delete[] valueBuffer;
            if (!ok) {
                break;
            }
        }

        delete cursor;
    } else {
        valueBuffer = d->db.get(sKey.data(), sKey.size(), &valueSize);
        if (valueBuffer) {
            resultHandler(valueBuffer, valueSize);
        } else {
            Error error(d->name.toStdString(), d->db.error().code(), d->db.error().message());
            errorHandler(error);
        }
        delete[] valueBuffer;
    }
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
