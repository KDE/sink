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

class Storage::Private
{
public:
    Private(const QString &s, const QString &n, AccessMode m, bool duplicates);
    ~Private();

    QString storageRoot;
    QString name;

    MDB_dbi dbi;
    MDB_env *env;
    MDB_txn *transaction;
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
      transaction(0),
      mode(m),
      readTransaction(false),
      firstOpen(true),
      allowDuplicates(duplicates)
{
    const QString fullPath(storageRoot + '/' + name);
    QDir dir;
    dir.mkpath(storageRoot);
    dir.mkdir(fullPath);

    //Ensure the environment is only created once
    QMutexLocker locker(&sMutex);

    int rc = 0;
    /*
     * It seems we can only ever have one environment open in the process. 
     * Otherwise multi-threading breaks.
     */
    env = sEnvironments.value(fullPath);
    if (!env) {
        if ((rc = mdb_env_create(&env))) {
            // TODO: handle error
            std::cerr << "mdb_env_create: " << rc << " " << mdb_strerror(rc) << std::endl;
        } else {
            if ((rc = mdb_env_open(env, fullPath.toStdString().data(), mode == ReadOnly ? MDB_RDONLY : 0 , 0664))) {
                std::cerr << "mdb_env_open: " << rc << " " << mdb_strerror(rc) << std::endl;
                mdb_env_close(env);
                env = 0;
            } else {
                //FIXME: dynamic resize
                const size_t dbSize = (size_t)10485760 * (size_t)100 * (size_t)80; //10MB * 800
                mdb_env_set_mapsize(env, dbSize);
                sEnvironments.insert(fullPath, env);
            }
        }
    }
}

Storage::Private::~Private()
{
    if (transaction) {
        mdb_txn_abort(transaction);
    }

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

bool Storage::isInTransaction() const
{
    return d->transaction;
}

bool Storage::startTransaction(AccessMode type,
                  const std::function<void(const Storage::Error &error)> &errorHandler)
{
    if (!d->env) {
        return false;
    }

    bool requestedRead = type == ReadOnly;

    if (d->mode == ReadOnly && !requestedRead) {
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, "Requested read/write transaction in read-only mode.");
        errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
        return false;
    }

    //We already have a transaction
    if (d->transaction && (!d->readTransaction || requestedRead)) {
        return true;
    }

    if (d->transaction) {
        // we are about to turn a read transaction into a writable one
        abortTransaction();
    }

    if (d->firstOpen && requestedRead) {
        //This is only required for named databases

        //A write transaction is at least required the first time
        // mdb_txn_begin(d->env, nullptr, 0, &d->transaction);
        //Open the database
        //With this we could open multiple named databases if we wanted to
        // mdb_dbi_open(d->transaction, nullptr, 0, &d->dbi);
        // mdb_txn_abort(d->transaction);
    }

    int rc;
    rc = mdb_txn_begin(d->env, NULL, requestedRead ? MDB_RDONLY : 0, &d->transaction);
    if (!rc) {
        rc = mdb_dbi_open(d->transaction, NULL, d->allowDuplicates ? MDB_DUPSORT : 0, &d->dbi);
        if (rc) {
            Error error(d->name.toLatin1(), ErrorCodes::GenericError, "Error while opening transaction: " + QByteArray(mdb_strerror(rc)));
            errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
        }
    } else {
        if (rc) {
            Error error(d->name.toLatin1(), ErrorCodes::GenericError, "Error while beginning transaction: " + QByteArray(mdb_strerror(rc)));
            errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
        }
    }

    d->firstOpen = false;
    d->readTransaction = requestedRead;
    return !rc;
}

bool Storage::commitTransaction(const std::function<void(const Storage::Error &error)> &errorHandler)
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
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, "Error during transaction commit: " + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
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

bool Storage::write(const void *keyPtr, size_t keySize, const void *valuePtr, size_t valueSize,
                  const std::function<void(const Storage::Error &error)> &errorHandler)
{
    if (!d->env) {
        Error error(d->name.toLatin1(), ErrorCodes::NotOpen, "Not open");
        errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
        return false;
    }

    if (d->mode == ReadOnly) {
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, "Tried to write in read-only mode.");
        errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
        return false;
    }

    if (!keyPtr || keySize == 0) {
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, "Tried to write empty key.");
        errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
        return false;
    }

    const bool implicitTransaction = !d->transaction || d->readTransaction;
    if (implicitTransaction) {
        if (!startTransaction()) {
            Error error(d->name.toLatin1(), ErrorCodes::TransactionError, "Failed to start transaction.");
            errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
            return false;
        }
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
        errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
    }

    if (implicitTransaction) {
        if (rc) {
            Error error(d->name.toLatin1(), ErrorCodes::GenericError, "aborting transaction");
            errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
            abortTransaction();
        } else {
            rc = commitTransaction();
        }
    }

    return !rc;
}

int Storage::scan(const QByteArray &k,
                  const std::function<bool(void *keyPtr, int keySize, void *valuePtr, int valueSize)> &resultHandler,
                  const std::function<void(const Storage::Error &error)> &errorHandler)
{
    if (!d->env) {
        Error error(d->name.toLatin1(), ErrorCodes::NotOpen, "Not open");
        errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
        return 0;
    }

    int rc;
    MDB_val key;
    MDB_val data;
    MDB_cursor *cursor;

    key.mv_data = (void*)k.constData();
    key.mv_size = k.size();

    const bool implicitTransaction = !d->transaction;
    if (implicitTransaction) {
        if (!startTransaction(ReadOnly)) {
            Error error(d->name.toLatin1(), ErrorCodes::TransactionError, "Could not start transaction");
            errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
            return 0;
        }
    }

    rc = mdb_cursor_open(d->transaction, d->dbi, &cursor);
    if (rc) {
        Error error(d->name.toLatin1(), getErrorCode(rc), QByteArray("Error during mdb_cursor open: ") + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
        return 0;
    }

    int numberOfRetrievedValues = 0;

    if (k.isEmpty() || d->allowDuplicates) {
        if ((rc = mdb_cursor_get(cursor, &key, &data, d->allowDuplicates ? MDB_SET_RANGE : MDB_FIRST)) == 0) {
            numberOfRetrievedValues++;
            if (resultHandler(key.mv_data, key.mv_size, data.mv_data, data.mv_size)) {
                while ((rc = mdb_cursor_get(cursor, &key, &data, d->allowDuplicates ? MDB_NEXT_DUP : MDB_NEXT)) == 0) {
                    numberOfRetrievedValues++;
                    if (!resultHandler(key.mv_data, key.mv_size, data.mv_data, data.mv_size)) {
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
            resultHandler(key.mv_data, key.mv_size, data.mv_data, data.mv_size);
        }
    }

    mdb_cursor_close(cursor);

    if (rc) {
        Error error(d->name.toLatin1(), getErrorCode(rc), QByteArray("Key: ") + k + " : " + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
    }

    if (implicitTransaction) {
        abortTransaction();
    }
    return numberOfRetrievedValues;
}

void Storage::remove(const QByteArray &key,
                     const std::function<void(const Storage::Error &error)> &errorHandler)
{
    remove(key.data(), key.size(), errorHandler);
}

void Storage::remove(const void *keyData, uint keySize,
                     const std::function<void(const Storage::Error &error)> &errorHandler)
{
    if (!d->env) {
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, "Not open");
        errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
        return;
    }

    if (d->mode == ReadOnly) {
        Error error(d->name.toLatin1(), ErrorCodes::ReadOnlyError, "Tried to write in read-only mode");
        errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
        return;
    }

    const bool implicitTransaction = !d->transaction || d->readTransaction;
    if (implicitTransaction) {
        if (!startTransaction()) {
            Error error(d->name.toLatin1(), ErrorCodes::TransactionError, "Could not start transaction");
            errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
            return;
        }
    }

    int rc;
    MDB_val key;
    key.mv_size = keySize;
    key.mv_data = const_cast<void*>(keyData);
    rc = mdb_del(d->transaction, d->dbi, &key, 0);

    if (rc) {
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, QString("Error on mdb_del: %1 %2").arg(rc).arg(mdb_strerror(rc)).toLatin1());
        errorHandler ? errorHandler(error) : defaultErrorHandler()(error);
    }

    if (implicitTransaction) {
        if (rc) {
            abortTransaction();
        } else {
            rc = commitTransaction();
        }
    }

    return;
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
