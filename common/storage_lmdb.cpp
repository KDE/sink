/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "storage.h"

#include <iostream>

#include <QAtomicInt>
#include <QDebug>
#include <QDir>
#include <QReadWriteLock>
#include <QString>
#include <QTime>
#include <QMutex>

#include <lmdb.h>

namespace Akonadi2
{

int getErrorCode(int e)
{
    switch (e) {
        case MDB_NOTFOUND:
            return Storage::ErrorCodes::NotFound;
        default:
            break;
    }
    return -1;
}

class Storage::NamedDatabase::Private
{
public:
    Private(const QByteArray &_db, bool _allowDuplicates, const std::function<void(const Storage::Error &error)> &_defaultErrorHandler, const QString &_name, MDB_txn *_txn)
        : db(_db),
        transaction(_txn),
        allowDuplicates(_allowDuplicates),
        defaultErrorHandler(_defaultErrorHandler),
        name(_name)
    {
    }

    ~Private()
    {

    }

    QByteArray db;
    MDB_txn *transaction;
    MDB_dbi dbi;
    bool allowDuplicates;
    std::function<void(const Storage::Error &error)> defaultErrorHandler;
    QString name;

    bool openDatabase(bool readOnly, std::function<void(const Storage::Error &error)> errorHandler)
    {
        unsigned int flags = 0;
        if (!readOnly) {
            flags |= MDB_CREATE;
        }
        if (allowDuplicates) {
            flags |= MDB_DUPSORT;
        }
        if (const int rc = mdb_dbi_open(transaction, db.constData(), flags, &dbi)) {
            dbi = 0;
            transaction = 0;
            //The database is not existing, ignore in read-only mode
            if (!(readOnly && rc == MDB_NOTFOUND)) {
                Error error(name.toLatin1(), ErrorCodes::GenericError, "Error while opening database: " + QByteArray(mdb_strerror(rc)));
                errorHandler ? errorHandler(error) : defaultErrorHandler(error);
            }
            return false;
        }
        return true;
    }
};

Storage::NamedDatabase::NamedDatabase()
    : d(nullptr)
{

}

Storage::NamedDatabase::NamedDatabase(NamedDatabase::Private *prv)
    : d(prv)
{
}

Storage::NamedDatabase::~NamedDatabase()
{
    delete d;
}

bool Storage::NamedDatabase::write(const QByteArray &sKey, const QByteArray &sValue, const std::function<void(const Storage::Error &error)> &errorHandler)
{
    if (!d || !d->transaction) {
        Error error("", ErrorCodes::GenericError, "Not open");
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        return false;
    }
    const void *keyPtr = sKey.data();
    const size_t keySize = sKey.size();
    const void *valuePtr = sValue.data();
    const size_t valueSize = sValue.size();

    if (!keyPtr || keySize == 0) {
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, "Tried to write empty key.");
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        return false;
    }

    int rc;
    MDB_val key, data;
    key.mv_size = keySize;
    key.mv_data = const_cast<void*>(keyPtr);
    data.mv_size = valueSize;
    data.mv_data = const_cast<void*>(valuePtr);
    rc = mdb_put(d->transaction, d->dbi, &key, &data, 0);

    if (rc) {
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, "mdb_put: " + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    }

    return !rc;
}

void Storage::NamedDatabase::remove(const QByteArray &k,
                     const std::function<void(const Storage::Error &error)> &errorHandler)
{
    if (!d || !d->transaction) {
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, "Not open");
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        return;
    }

    int rc;
    MDB_val key;
    key.mv_size = k.size();
    key.mv_data = const_cast<void*>(static_cast<const void*>(k.data()));
    rc = mdb_del(d->transaction, d->dbi, &key, 0);

    if (rc) {
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, QString("Error on mdb_del: %1 %2").arg(rc).arg(mdb_strerror(rc)).toLatin1());
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    }

    return;
}

int Storage::NamedDatabase::scan(const QByteArray &k,
                  const std::function<bool(const QByteArray &key, const QByteArray &value)> &resultHandler,
                  const std::function<void(const Storage::Error &error)> &errorHandler) const
{
    if (!d || !d->transaction) {
        //Not an error. We rely on this to read nothing from non-existing databases.
        return 0;
    }

    int rc;
    MDB_val key;
    MDB_val data;
    MDB_cursor *cursor;

    key.mv_data = (void*)k.constData();
    key.mv_size = k.size();

    rc = mdb_cursor_open(d->transaction, d->dbi, &cursor);
    if (rc) {
        Error error(d->name.toLatin1(), getErrorCode(rc), QByteArray("Error during mdb_cursor open: ") + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        return 0;
    }

    int numberOfRetrievedValues = 0;

    if (k.isEmpty() || d->allowDuplicates) {
        if ((rc = mdb_cursor_get(cursor, &key, &data, d->allowDuplicates ? MDB_SET : MDB_FIRST)) == 0) {
            numberOfRetrievedValues++;
            if (resultHandler(QByteArray::fromRawData((char*)key.mv_data, key.mv_size), QByteArray::fromRawData((char*)data.mv_data, data.mv_size))) {
                while ((rc = mdb_cursor_get(cursor, &key, &data, d->allowDuplicates ? MDB_NEXT_DUP : MDB_NEXT)) == 0) {
                    numberOfRetrievedValues++;
                    if (!resultHandler(QByteArray::fromRawData((char*)key.mv_data, key.mv_size), QByteArray::fromRawData((char*)data.mv_data, data.mv_size))) {
                        break;
                    }
                }
            }
        }

        //We never find the last value
        if (rc == MDB_NOTFOUND) {
            rc = 0;
        }
    } else {
        if ((rc = mdb_cursor_get(cursor, &key, &data, MDB_SET)) == 0) {
            numberOfRetrievedValues++;
            resultHandler(QByteArray::fromRawData((char*)key.mv_data, key.mv_size), QByteArray::fromRawData((char*)data.mv_data, data.mv_size));
        }
    }

    mdb_cursor_close(cursor);

    if (rc) {
        Error error(d->name.toLatin1(), getErrorCode(rc), QByteArray("Key: ") + k + " : " + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    }

    return numberOfRetrievedValues;
}




class Storage::Transaction::Private
{
public:
    Private(bool _requestRead, bool _allowDuplicates, const std::function<void(const Storage::Error &error)> &_defaultErrorHandler, const QString &_name, MDB_env *_env)
        : env(_env),
        requestedRead(_requestRead),
        allowDuplicates(_allowDuplicates),
        defaultErrorHandler(_defaultErrorHandler),
        name(_name),
        implicitCommit(false),
        error(false),
        modificationCounter(0)
    {

    }
    ~Private()
    {

    }

    MDB_env *env;
    MDB_txn *transaction;
    MDB_dbi dbi;
    bool requestedRead;
    bool allowDuplicates;
    std::function<void(const Storage::Error &error)> defaultErrorHandler;
    QString name;
    bool implicitCommit;
    bool error;
    int modificationCounter;

    void startTransaction()
    {
        // qDebug() << "Opening transaction " << requestedRead;
        const int rc = mdb_txn_begin(env, NULL, requestedRead ? MDB_RDONLY : 0, &transaction);
        if (rc) {
            defaultErrorHandler(Error(name.toLatin1(), ErrorCodes::GenericError, "Error while opening transaction: " + QByteArray(mdb_strerror(rc))));
        }
    }
};

Storage::Transaction::Transaction()
    : d(nullptr)
{

}

Storage::Transaction::Transaction(Transaction::Private *prv)
    : d(prv)
{
    d->startTransaction();
}

Storage::Transaction::~Transaction()
{
    if (d && d->transaction) {
        if (d->implicitCommit && !d->error) {
            // qDebug() << "implicit commit";
            commit();
        } else {
            // qDebug() << "Aorting transaction";
            mdb_txn_abort(d->transaction);
        }
    }
    delete d;
}

bool Storage::Transaction::commit(const std::function<void(const Storage::Error &error)> &errorHandler)
{
    if (!d || !d->transaction) {
        return false;
    }

    const int rc = mdb_txn_commit(d->transaction);
    if (rc) {
        mdb_txn_abort(d->transaction);
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, "Error during transaction commit: " + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    }
    d->transaction = nullptr;

    return !rc;
}

void Storage::Transaction::abort()
{
    if (!d || !d->transaction) {
        return;
    }

    mdb_txn_abort(d->transaction);
    d->transaction = nullptr;
}

Storage::NamedDatabase Storage::Transaction::openDatabase(const QByteArray &db, const std::function<void(const Storage::Error &error)> &errorHandler, bool allowDuplicates) const
{
    if (!d) {
        return Storage::NamedDatabase();
    }
    //We don't now if anything changed
    d->implicitCommit = true;
    auto p = new Storage::NamedDatabase::Private(db, allowDuplicates, d->defaultErrorHandler, d->name, d->transaction);
    if (!p->openDatabase(d->requestedRead, errorHandler)) {
        return Storage::NamedDatabase();
        delete p;
    }
    return Storage::NamedDatabase(p);
}

bool Storage::Transaction::write(const QByteArray &key, const QByteArray &value, const std::function<void(const Storage::Error &error)> &errorHandler)
{
    auto eHandler = [this, errorHandler](const Storage::Error &error) {
        d->error = true;
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    };
    openDatabase("default", eHandler, d->allowDuplicates).write(key, value, eHandler);
    d->implicitCommit = true;

    return !d->error;
}

void Storage::Transaction::remove(const QByteArray &k,
                     const std::function<void(const Storage::Error &error)> &errorHandler)
{
    auto eHandler = [this, errorHandler](const Storage::Error &error) {
        d->error = true;
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    };
    openDatabase("default", eHandler, d->allowDuplicates).remove(k, eHandler);
    d->implicitCommit = true;
}

int Storage::Transaction::scan(const QByteArray &k,
                  const std::function<bool(const QByteArray &key, const QByteArray &value)> &resultHandler,
                  const std::function<void(const Storage::Error &error)> &errorHandler) const
{
    auto db = openDatabase("default", std::function<void(const Storage::Error &error)>(), d->allowDuplicates);
    if (db) {
        return db.scan(k, resultHandler, errorHandler);
    }
    return 0;
}









class Storage::Private
{
public:
    Private(const QString &s, const QString &n, AccessMode m, bool duplicates);
    ~Private();

    QString storageRoot;
    QString name;

    MDB_env *env;
    AccessMode mode;
    bool readTransaction;
    bool firstOpen;
    bool allowDuplicates;
    static QMutex sMutex;
    static QHash<QString, MDB_env*> sEnvironments;
};

QMutex Storage::Private::sMutex;
QHash<QString, MDB_env*> Storage::Private::sEnvironments;

Storage::Private::Private(const QString &s, const QString &n, AccessMode m, bool duplicates)
    : storageRoot(s),
      name(n),
      env(0),
      mode(m),
      readTransaction(false),
      firstOpen(true),
      allowDuplicates(duplicates)
{
    const QString fullPath(storageRoot + '/' + name);
    QFileInfo dirInfo(fullPath);
    if (!dirInfo.exists() && mode == ReadWrite) {
        QDir().mkpath(fullPath);
        dirInfo.refresh();
    }
    if (mode == ReadWrite && !dirInfo.permission(QFile::WriteOwner)) {
        qCritical() << fullPath << "does not have write permissions. Aborting";
    } else if (dirInfo.exists()) {
        //Ensure the environment is only created once
        QMutexLocker locker(&sMutex);

        /*
        * It seems we can only ever have one environment open in the process. 
        * Otherwise multi-threading breaks.
        */
        env = sEnvironments.value(fullPath);
        if (!env) {
            int rc = 0;
            if ((rc = mdb_env_create(&env))) {
                // TODO: handle error
                std::cerr << "mdb_env_create: " << rc << " " << mdb_strerror(rc) << std::endl;
            } else {
                mdb_env_set_maxdbs(env, 50);
                unsigned int flags = MDB_NOTLS;
                if (mode == ReadOnly) {
                    flags |= MDB_RDONLY;
                }
                if ((rc = mdb_env_open(env, fullPath.toStdString().data(), flags, 0664))) {
                    std::cerr << "mdb_env_open: " << rc << " " << mdb_strerror(rc) << std::endl;
                    mdb_env_close(env);
                    env = 0;
                } else {
                    //FIXME: dynamic resize
                    const size_t dbSize = (size_t)10485760 * (size_t)8000; //1MB * 8000
                    mdb_env_set_mapsize(env, dbSize);
                    sEnvironments.insert(fullPath, env);
                }
            }
        }
    }
}

Storage::Private::~Private()
{
    //Since we can have only one environment open per process, we currently leak the environments.
    // if (env) {
    //     //mdb_dbi_close should not be necessary and is potentially dangerous (see docs)
    //     mdb_dbi_close(env, dbi);
    //     mdb_env_close(env);
    // }
}

Storage::Storage(const QString &storageRoot, const QString &name, AccessMode mode, bool allowDuplicates)
    : d(new Private(storageRoot, name, mode, allowDuplicates))
{
}

Storage::~Storage()
{
    delete d;
}

bool Storage::exists() const
{
    return (d->env != 0);
}

Storage::Transaction Storage::createTransaction(AccessMode type, const std::function<void(const Storage::Error &error)> &errorHandlerArg)
{
    auto errorHandler = errorHandlerArg ? errorHandlerArg : defaultErrorHandler();
    if (!d->env) {
        errorHandler(Error(d->name.toLatin1(), ErrorCodes::GenericError, "Missing database environment"));
        return Transaction();
    }

    bool requestedRead = type == ReadOnly;

    if (d->mode == ReadOnly && !requestedRead) {
        errorHandler(Error(d->name.toLatin1(), ErrorCodes::GenericError, "Requested read/write transaction in read-only mode."));
        return Transaction();
    }

    return Transaction(new Transaction::Private(requestedRead, d->allowDuplicates, defaultErrorHandler(), d->name, d->env));
}

qint64 Storage::diskUsage() const
{
    QFileInfo info(d->storageRoot + '/' + d->name + "/data.mdb");
    return info.size();
}

void Storage::removeFromDisk() const
{
    const QString fullPath(d->storageRoot + '/' + d->name);
    QMutexLocker locker(&d->sMutex);
    QDir dir(fullPath);
    if (!dir.removeRecursively()) {
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, QString("Failed to remove directory %1 %2").arg(d->storageRoot).arg(d->name).toLatin1());
        defaultErrorHandler()(error);
    }
    auto env = d->sEnvironments.take(fullPath);
    mdb_env_close(env);
}

} // namespace Akonadi2
