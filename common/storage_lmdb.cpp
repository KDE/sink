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

#include <QDebug>
#include <QDir>
#include <QReadWriteLock>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QTime>
#include <valgrind.h>
#include <lmdb.h>
#include "log.h"

#ifdef Q_OS_WIN
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

namespace Sink {
namespace Storage {

static QReadWriteLock sDbisLock;
static QReadWriteLock sEnvironmentsLock;
static QMutex sCreateDbiLock;
static QHash<QString, MDB_env *> sEnvironments;
static QHash<QString, MDB_dbi> sDbis;

int AllowDuplicates = MDB_DUPSORT;
int IntegerKeys = MDB_INTEGERKEY;
int IntegerValues = MDB_INTEGERDUP;

int getErrorCode(int e)
{
    switch (e) {
        case MDB_NOTFOUND:
            return DataStore::ErrorCodes::NotFound;
        default:
            break;
    }
    return -1;
}

static QList<QByteArray> getDatabaseNames(MDB_txn *transaction)
{
    if (!transaction) {
        SinkWarning() << "Invalid transaction";
        return QList<QByteArray>();
    }
    int rc;
    QList<QByteArray> list;
    MDB_dbi dbi;
    if ((rc = mdb_dbi_open(transaction, nullptr, 0, &dbi) == 0)) {
        MDB_val key;
        MDB_val data;
        MDB_cursor *cursor;

        mdb_cursor_open(transaction, dbi, &cursor);
        if ((rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST)) == 0) {
            list << QByteArray::fromRawData((char *)key.mv_data, key.mv_size);
            while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
                list << QByteArray::fromRawData((char *)key.mv_data, key.mv_size);
            }
        } else {
            //Normal if we don't have any databases yet
            if (rc == MDB_NOTFOUND) {
                rc = 0;
            }
            if (rc) {
                SinkWarning() << "Failed to get a value" << rc;
            }
        }
        mdb_cursor_close(cursor);
    } else {
        SinkWarning() << "Failed to open db" << rc << QByteArray(mdb_strerror(rc));
    }
    return list;

}

/*
 * To create a dbi we always need a write transaction,
 * and we always need to commit the transaction ASAP
 * We can only ever enter from one point per process.
 */
static bool createDbi(MDB_txn *transaction, const QByteArray &db, bool readOnly, int flags, MDB_dbi &dbi)
{
    MDB_dbi flagtableDbi;
    if (const int rc = mdb_dbi_open(transaction, "__flagtable", readOnly ? 0 : MDB_CREATE, &flagtableDbi)) {
        if (!readOnly) {
            SinkWarning() << "Failed to to open flagdb: " << QByteArray(mdb_strerror(rc));
        }
    } else {
        MDB_val key, value;
        key.mv_data = const_cast<void*>(static_cast<const void*>(db.constData()));
        key.mv_size = db.size();
        if (const auto rc = mdb_get(transaction, flagtableDbi, &key, &value)) {
            //We expect this to fail for new databases
            if (rc != MDB_NOTFOUND) {
                SinkWarning() << "Failed to read flags from flag db: " << QByteArray(mdb_strerror(rc));
            }
        } else {
            //Found the flags
            const auto ba = QByteArray::fromRawData((char *)value.mv_data, value.mv_size);
            flags = ba.toInt();
        }
    }

    if (flags & IntegerValues && !(flags & AllowDuplicates)) {
        SinkWarning() << "Opening a database with integer values, but not duplicate keys";
    }

    if (const int rc = mdb_dbi_open(transaction, db.constData(), flags, &dbi)) {
        //Create the db if it is not existing already
        if (rc == MDB_NOTFOUND && !readOnly) {
            //Sanity check db name
            {
                auto parts = db.split('.');
                for (const auto &p : parts) {
                    auto containsSpecialCharacter = [] (const QByteArray &p) {
                        for (int i = 0; i < p.size(); i++) {
                            const auto c = p.at(i);
                            //Between 0 and z in the ascii table. Essentially ensures that the name is printable and doesn't contain special chars
                            if (c < 0x30 || c > 0x7A) {
                                return true;
                            }
                        }
                        return false;
                    };
                    if (p.isEmpty() || containsSpecialCharacter(p)) {
                        SinkError() << "Tried to create a db with an invalid name. Hex:" << db.toHex() << " ASCII:" << db;
                        Q_ASSERT(false);
                        throw std::runtime_error("Fatal error while creating db.");
                    }
                }
            }
            if (const int rc = mdb_dbi_open(transaction, db.constData(), flags | MDB_CREATE, &dbi)) {
                SinkWarning() << "Failed to create db " << QByteArray(mdb_strerror(rc));
                return false;
            }
            //Record the db flags
            MDB_val key, value;
            key.mv_data = const_cast<void*>(static_cast<const void*>(db.constData()));
            key.mv_size = db.size();
            //Store the flags without the create option
            const auto ba = QByteArray::number(flags);
            value.mv_data = const_cast<void*>(static_cast<const void*>(ba.constData()));
            value.mv_size = ba.size();
            if (const int rc = mdb_put(transaction, flagtableDbi, &key, &value, MDB_NOOVERWRITE)) {
                //We expect this to fail if we're only creating the dbi but not the db
                if (rc != MDB_KEYEXIST) {
                    SinkWarning() << "Failed to write flags to flag db: " << QByteArray(mdb_strerror(rc));
                }
            }
        } else {
            //It's not an error if we only want to read
            if (!readOnly) {
                SinkWarning() << "Failed to open db " << db << "error:" << QByteArray(mdb_strerror(rc));
                return true;
            }
            return false;
        }
    }
    return true;
}

class DataStore::NamedDatabase::Private
{
public:
    Private(const QByteArray &_db, int _flags,
        const std::function<void(const DataStore::Error &error)> &_defaultErrorHandler,
        const QString &_name, MDB_txn *_txn)
        : db(_db),
          transaction(_txn),
          flags(_flags),
          defaultErrorHandler(_defaultErrorHandler),
          name(_name)
    {
    }

    ~Private() = default;

    QByteArray db;
    MDB_txn *transaction;
    MDB_dbi dbi{0};
    int flags;
    std::function<void(const DataStore::Error &error)> defaultErrorHandler;
    QString name;
    bool createdNewDbi = false;
    QString createdNewDbiName;

    bool dbiValidForTransaction(MDB_dbi dbi, MDB_txn *transaction)
    {
        //sDbis can contain dbi's that are not available to this transaction.
        //We use mdb_dbi_flags to check if the dbi is valid for this transaction.
        uint f;
        if (mdb_dbi_flags(transaction, dbi, &f) == EINVAL) {
            return false;
        }
        return true;
    }

    bool openDatabase(bool readOnly, std::function<void(const DataStore::Error &error)> errorHandler)
    {
        const auto dbiName = name + db;
        //Never access sDbis while anything is writing to it.
        QReadLocker dbiLocker{&sDbisLock};
        if (sDbis.contains(dbiName)) {
            dbi = sDbis.value(dbiName);
            //sDbis can potentially contain a dbi that is not valid for this transaction, if this transaction was created before the dbi was created.
            if (dbiValidForTransaction(dbi, transaction)) {
                return true;
            } else {
                SinkTrace() << "Found dbi that is not available for the current transaction.";
                if (readOnly) {
                    //Recovery for read-only transactions. Abort and renew.
                    mdb_txn_reset(transaction);
                    mdb_txn_renew(transaction);
                    Q_ASSERT(dbiValidForTransaction(dbi, transaction));
                    return true;
                }
                //There is no recover path for non-read-only transactions.
            }
            //Nothing in the code deals well with non-existing databases.
            Q_ASSERT(false);
            return false;
        }


        /*
        * Dynamic creation of databases.
        * If all databases were defined via the database layout we wouldn't ever end up in here.
        * However, we rely on this codepath for indexes, synchronization databases and in race-conditions
        * where the database is not yet fully created when the client initializes it for reading.
        *
        * There are a few things to consider:
        * * dbi's (DataBase Identifier) should be opened once (ideally), and then be persisted in the environment.
        * * To open a dbi we need a transaction and must commit the transaction. From then on any open transaction will have access to the dbi.
        * * Already running transactions will not have access to the dbi.
        * * There *must* only ever be one active transaction opening dbi's (using mdb_dbi_open), and that transaction *must*
        * commit or abort before any other transaction opens a dbi.
        *
        * We solve this the following way:
        * * For read-only transactions we abort the transaction, open the dbi and persist it in the environment, and reopen the transaction (so the dbi is available). This may result in the db content changing unexpectedly and referenced memory becoming unavailable, but isn't a problem as long as we don't rely on memory remaining valid for the duration of the transaction (which is anyways not given since any operation would invalidate the memory region)..
        * * For write transactions we open the dbi for future use, and then open it as well in the current transaction.
        * * Write transactions that open the named database multiple times will call this codepath multiple times,
        * this is ok though because the same dbi will be returned by mdb_dbi_open (We could also start to do a lookup in
        * Transaction::Private::createdDbs first).
        */
        SinkTrace() << "Creating database dynamically: " << dbiName << readOnly;
        //Only one transaction may ever create dbis at a time.
        while (!sCreateDbiLock.tryLock(10)) {
            //Allow another thread that has already acquired sCreateDbiLock to continue below.
            //Otherwise we risk a dead-lock if another thread already acquired sCreateDbiLock, but then lost the sDbisLock while upgrading it to a
            //write lock below
            dbiLocker.unlock();
            dbiLocker.relock();
        }
        //Double checked locking
        if (sDbis.contains(dbiName)) {
            dbi = sDbis.value(dbiName);
            //sDbis can potentially contain a dbi that is not valid for this transaction, if this transaction was created before the dbi was created.
            sCreateDbiLock.unlock();
            if (dbiValidForTransaction(dbi, transaction)) {
                return true;
            } else {
                SinkTrace() << "Found dbi that is not available for the current transaction.";
                if (readOnly) {
                    //Recovery for read-only transactions. Abort and renew.
                    mdb_txn_reset(transaction);
                    mdb_txn_renew(transaction);
                    Q_ASSERT(dbiValidForTransaction(dbi, transaction));
                    return true;
                }
                //There is no recover path for non-read-only transactions.
                Q_ASSERT(false);
                return false;
            }
        }

        //Ensure nobody reads sDbis either
        dbiLocker.unlock();
        //We risk loosing the lock in here. That's why we tryLock above in the while loop
        QWriteLocker dbiWriteLocker(&sDbisLock);

        //Create a transaction to open the dbi
        MDB_txn *dbiTransaction;
        if (readOnly) {
            MDB_env *env = mdb_txn_env(transaction);
            Q_ASSERT(env);
            mdb_txn_reset(transaction);
            if (const int rc = mdb_txn_begin(env, nullptr, MDB_RDONLY, &dbiTransaction)) {
                SinkError() << "Failed to open transaction: " << QByteArray(mdb_strerror(rc)) << readOnly << transaction;
                sCreateDbiLock.unlock();
                return false;
            }
        } else {
            dbiTransaction = transaction;
        }
        if (createDbi(dbiTransaction, db, readOnly, flags, dbi)) {
            if (readOnly) {
                mdb_txn_commit(dbiTransaction);
                Q_ASSERT(!sDbis.contains(dbiName));
                sDbis.insert(dbiName, dbi);
                //We reopen the read-only transaction so the dbi becomes available in it.
                mdb_txn_renew(transaction);
            } else {
                createdNewDbi = true;
                createdNewDbiName = dbiName;
            }
            //Ensure the dbi is valid for the parent transaction
            Q_ASSERT(dbiValidForTransaction(dbi, transaction));
        } else {
            if (readOnly) {
                mdb_txn_abort(dbiTransaction);
                mdb_txn_renew(transaction);
            } else {
                SinkWarning() << "Failed to create the dbi: " << dbiName;
            }
            dbi = 0;
            transaction = 0;
            sCreateDbiLock.unlock();
            return false;
        }
        sCreateDbiLock.unlock();
        return true;
    }
};

DataStore::NamedDatabase::NamedDatabase() : d(nullptr)
{
}

DataStore::NamedDatabase::NamedDatabase(NamedDatabase::Private *prv) : d(prv)
{
}

DataStore::NamedDatabase::NamedDatabase(NamedDatabase &&other) : d(nullptr)
{
    *this = std::move(other);
}

DataStore::NamedDatabase &DataStore::NamedDatabase::operator=(DataStore::NamedDatabase &&other)
{
    if (&other != this) {
        delete d;
        d = other.d;
        other.d = nullptr;
    }
    return *this;
}

DataStore::NamedDatabase::~NamedDatabase()
{
    delete d;
}

bool DataStore::NamedDatabase::write(const size_t key, const QByteArray &value,
    const std::function<void(const DataStore::Error &error)> &errorHandler)
{
    return write(sizeTToByteArray(key), value, errorHandler);
}

bool DataStore::NamedDatabase::write(const QByteArray &sKey, const QByteArray &sValue, const std::function<void(const DataStore::Error &error)> &errorHandler)
{
    if (!d || !d->transaction) {
        Error error("", ErrorCodes::GenericError, "Not open");
        if (d) {
            errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        }
        return false;
    }
    const void *keyPtr = sKey.data();
    const size_t keySize = sKey.size();
    const void *valuePtr = sValue.data();
    const size_t valueSize = sValue.size();

    if (!keyPtr || keySize == 0) {
        Error error(d->name.toLatin1() + d->db, ErrorCodes::GenericError, "Tried to write empty key.");
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        return false;
    }

    int rc;
    MDB_val key, data;
    key.mv_size = keySize;
    key.mv_data = const_cast<void *>(keyPtr);
    data.mv_size = valueSize;
    data.mv_data = const_cast<void *>(valuePtr);
    rc = mdb_put(d->transaction, d->dbi, &key, &data, 0);

    if (rc) {
        Error error(d->name.toLatin1() + d->db, ErrorCodes::GenericError, "mdb_put: " + QByteArray(mdb_strerror(rc)) + " Key: " + sKey + " Value: " + sValue);
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    }

    return !rc;
}

void DataStore::NamedDatabase::remove(
    const size_t key, const std::function<void(const DataStore::Error &error)> &errorHandler)
{
    return remove(sizeTToByteArray(key), errorHandler);
}

void DataStore::NamedDatabase::remove(const QByteArray &k, const std::function<void(const DataStore::Error &error)> &errorHandler)
{
    remove(k, QByteArray(), errorHandler);
}

void DataStore::NamedDatabase::remove(const size_t key, const QByteArray &value,
    const std::function<void(const DataStore::Error &error)> &errorHandler)
{
    return remove(sizeTToByteArray(key), value, errorHandler);
}

void DataStore::NamedDatabase::remove(const QByteArray &k, const QByteArray &value, const std::function<void(const DataStore::Error &error)> &errorHandler)
{
    if (!d || !d->transaction) {
        if (d) {
            Error error(d->name.toLatin1() + d->db, ErrorCodes::GenericError, "Not open");
            errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        }
        return;
    }

    int rc;
    MDB_val key;
    key.mv_size = k.size();
    key.mv_data = const_cast<void *>(static_cast<const void *>(k.data()));
    if (value.isEmpty()) {
        rc = mdb_del(d->transaction, d->dbi, &key, 0);
    } else {
        MDB_val data;
        data.mv_size = value.size();
        data.mv_data = const_cast<void *>(static_cast<const void *>(value.data()));
        rc = mdb_del(d->transaction, d->dbi, &key, &data);
    }

    if (rc) {
        auto errorCode = ErrorCodes::GenericError;
        if (rc == MDB_NOTFOUND) {
            errorCode = ErrorCodes::NotFound;
        }
        Error error(d->name.toLatin1() + d->db, errorCode, QString("Error on mdb_del: %1 %2").arg(rc).arg(mdb_strerror(rc)).toLatin1());
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    }
}

int DataStore::NamedDatabase::scan(const size_t key,
    const std::function<bool(size_t key, const QByteArray &value)> &resultHandler,
    const std::function<void(const DataStore::Error &error)> &errorHandler) const
{
    return scan(sizeTToByteArray(key),
        [&resultHandler](const QByteArray &key, const QByteArray &value) {
            return resultHandler(byteArrayToSizeT(key), value);
        },
        errorHandler, /* findSubstringKeys = */ false);
}

int DataStore::NamedDatabase::scan(const QByteArray &k, const std::function<bool(const QByteArray &key, const QByteArray &value)> &resultHandler,
    const std::function<void(const DataStore::Error &error)> &errorHandler, bool findSubstringKeys) const
{
    if (!d || !d->transaction) {
        // Not an error. We rely on this to read nothing from non-existing databases.
        return 0;
    }

    int rc;
    MDB_val key;
    MDB_val data;
    MDB_cursor *cursor;

    key.mv_data = (void *)k.constData();
    key.mv_size = k.size();

    rc = mdb_cursor_open(d->transaction, d->dbi, &cursor);
    if (rc) {
        //Invalid arguments can mean that the transaction doesn't contain the db dbi
        Error error(d->name.toLatin1() + d->db, getErrorCode(rc), QByteArray("Error during mdb_cursor_open: ") + QByteArray(mdb_strerror(rc)) + ". Key: " + k);
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        return 0;
    }

    int numberOfRetrievedValues = 0;

    const bool allowDuplicates = d->flags & AllowDuplicates;
    const bool emptyKey = k.isEmpty();

    if (emptyKey || allowDuplicates || findSubstringKeys) {
        const MDB_cursor_op op = [&] {
            if (emptyKey) {
                return MDB_FIRST;
            }
            if (findSubstringKeys) {
                return MDB_SET_RANGE;
            }
            return MDB_SET;
        }();
        const MDB_cursor_op nextOp = (allowDuplicates && !findSubstringKeys && !emptyKey) ? MDB_NEXT_DUP : MDB_NEXT;

        if ((rc = mdb_cursor_get(cursor, &key, &data, op)) == 0) {
            const auto current = QByteArray::fromRawData((char *)key.mv_data, key.mv_size);
            // The first lookup will find a key that is equal or greather than our key
            if (current.startsWith(k)) {
                numberOfRetrievedValues++;
                if (resultHandler(current, QByteArray::fromRawData((char *)data.mv_data, data.mv_size))) {
                    if (findSubstringKeys) {
                        // Reset the key to what we search for
                        key.mv_data = (void *)k.constData();
                        key.mv_size = k.size();
                    }
                    while ((rc = mdb_cursor_get(cursor, &key, &data, nextOp)) == 0) {
                        const auto current = QByteArray::fromRawData((char *)key.mv_data, key.mv_size);
                        // Every consequitive lookup simply iterates through the list
                        if (current.startsWith(k)) {
                            numberOfRetrievedValues++;
                            if (!resultHandler(current, QByteArray::fromRawData((char *)data.mv_data, data.mv_size))) {
                                break;
                            }
                        }
                    }
                }
            }
        }

        // We never find the last value
        if (rc == MDB_NOTFOUND) {
            rc = 0;
        }
    } else {
        if ((rc = mdb_cursor_get(cursor, &key, &data, MDB_SET)) == 0) {
            numberOfRetrievedValues++;
            resultHandler(QByteArray::fromRawData((char *)key.mv_data, key.mv_size), QByteArray::fromRawData((char *)data.mv_data, data.mv_size));
        }
    }

    mdb_cursor_close(cursor);

    if (rc) {
        Error error(d->name.toLatin1() + d->db, getErrorCode(rc), QByteArray("Error during scan. Key: ") + k + " : " + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    }

    return numberOfRetrievedValues;
}


void DataStore::NamedDatabase::findLatest(size_t key,
    const std::function<void(size_t key, const QByteArray &value)> &resultHandler,
    const std::function<void(const DataStore::Error &error)> &errorHandler) const
{
    return findLatest(sizeTToByteArray(key),
        [&resultHandler](const QByteArray &key, const QByteArray &value) {
            resultHandler(byteArrayToSizeT(value), value);
        },
        errorHandler);
}

void DataStore::NamedDatabase::findLatest(const QByteArray &k, const std::function<void(const QByteArray &key, const QByteArray &value)> &resultHandler,
    const std::function<void(const DataStore::Error &error)> &errorHandler) const
{
    if (!d || !d->transaction) {
        // Not an error. We rely on this to read nothing from non-existing databases.
        return;
    }
    if (k.isEmpty()) {
        Error error(d->name.toLatin1() + d->db, GenericError, QByteArray("Can't use findLatest with empty key."));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        return;
    }

    int rc;
    MDB_val key;
    MDB_val data;
    MDB_cursor *cursor;

    key.mv_data = (void *)k.constData();
    key.mv_size = k.size();

    rc = mdb_cursor_open(d->transaction, d->dbi, &cursor);
    if (rc) {
        Error error(d->name.toLatin1() + d->db, getErrorCode(rc), QByteArray("Error during mdb_cursor_open: ") + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        return;
    }

    bool foundValue = false;
    MDB_cursor_op op = MDB_SET_RANGE;
    if ((rc = mdb_cursor_get(cursor, &key, &data, op)) == 0) {
        // The first lookup will find a key that is equal or greather than our key
        if (QByteArray::fromRawData((char *)key.mv_data, key.mv_size).startsWith(k)) {
            //Read next value until we no longer match
            while (QByteArray::fromRawData((char *)key.mv_data, key.mv_size).startsWith(k)) {
                MDB_cursor_op nextOp = MDB_NEXT;
                rc = mdb_cursor_get(cursor, &key, &data, nextOp);
                if (rc) {
                    break;
                }
            }
            //Now read the previous value, and that's the latest one
            MDB_cursor_op prefOp = MDB_PREV;
            // We read past the end above, just take the last value
            if (rc == MDB_NOTFOUND) {
                prefOp = MDB_LAST;
            }
            rc = mdb_cursor_get(cursor, &key, &data, prefOp);
            if (!rc) {
                foundValue = true;
                resultHandler(QByteArray::fromRawData((char *)key.mv_data, key.mv_size), QByteArray::fromRawData((char *)data.mv_data, data.mv_size));
            }
        }
    }

    // We never find the last value
    if (rc == MDB_NOTFOUND) {
        rc = 0;
    }

    mdb_cursor_close(cursor);

    if (rc) {
        Error error(d->name.toLatin1(), getErrorCode(rc), QByteArray("Error during find latest. Key: ") + k + " : " + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    } else if (!foundValue) {
        Error error(d->name.toLatin1(), 1, QByteArray("Error during find latest. Key: ") + k + " : No value found");
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    }

    return;
}

void DataStore::NamedDatabase::findLast(const QByteArray &k, const std::function<void(const QByteArray &key, const QByteArray &value)> &resultHandler,
    const std::function<void(const DataStore::Error &error)> &errorHandler) const
{
    if (!d || !d->transaction) {
        // Not an error. We rely on this to read nothing from non-existing databases.
        return;
    }
    if (k.isEmpty()) {
        Error error(d->name.toLatin1() + d->db, GenericError, QByteArray("Can't use findLatest with empty key."));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        return;
    }

    int rc;
    MDB_val key;
    MDB_val data;
    MDB_cursor *cursor;

    key.mv_data = (void *)k.constData();
    key.mv_size = k.size();

    rc = mdb_cursor_open(d->transaction, d->dbi, &cursor);
    if (rc) {
        Error error(d->name.toLatin1() + d->db, getErrorCode(rc), QByteArray("Error during mdb_cursor_open: ") + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        return;
    }

    bool foundValue = false;
    if ((rc = mdb_cursor_get(cursor, &key, &data, MDB_SET)) == 0) {
        if ((rc = mdb_cursor_get(cursor, &key, &data, MDB_LAST_DUP)) == 0) {
            foundValue = true;
            resultHandler(QByteArray::fromRawData((char *)key.mv_data, key.mv_size), QByteArray::fromRawData((char *)data.mv_data, data.mv_size));
        }
    }

    mdb_cursor_close(cursor);

    if (rc) {
        Error error(d->name.toLatin1(), getErrorCode(rc), QByteArray("Error during find latest. Key: ") + k + " : " + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    } else if (!foundValue) {
        Error error(d->name.toLatin1(), 1, QByteArray("Error during find latest. Key: ") + k + " : No value found");
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    }

    return;
}

int DataStore::NamedDatabase::findAllInRange(const size_t lowerBound, const size_t upperBound,
    const std::function<void(size_t key, const QByteArray &value)> &resultHandler,
    const std::function<void(const DataStore::Error &error)> &errorHandler) const
{
    return findAllInRange(sizeTToByteArray(lowerBound), sizeTToByteArray(upperBound),
        [&resultHandler](const QByteArray &key, const QByteArray &value) {
            resultHandler(byteArrayToSizeT(value), value);
        },
        errorHandler);
}

int DataStore::NamedDatabase::findAllInRange(const QByteArray &lowerBound, const QByteArray &upperBound,
    const std::function<void(const QByteArray &key, const QByteArray &value)> &resultHandler,
    const std::function<void(const DataStore::Error &error)> &errorHandler) const
{
    if (!d || !d->transaction) {
        // Not an error. We rely on this to read nothing from non-existing databases.
        return 0;
    }

    MDB_cursor *cursor;
    if (int rc = mdb_cursor_open(d->transaction, d->dbi, &cursor)) {
        // Invalid arguments can mean that the transaction doesn't contain the db dbi
        Error error(d->name.toLatin1() + d->db, getErrorCode(rc),
            QByteArray("Error during mdb_cursor_open: ") + QByteArray(mdb_strerror(rc)) +
                ". Lower bound: " + lowerBound + " Upper bound: " + upperBound);
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        return 0;
    }

    MDB_val firstKey = {(size_t)lowerBound.size(), (void *)lowerBound.constData()};
    MDB_val idealLastKey = {(size_t)upperBound.size(), (void *)upperBound.constData()};
    MDB_val currentKey;
    MDB_val data;

    // Find the first key in the range
    int rc = mdb_cursor_get(cursor, &firstKey, &data, MDB_SET_RANGE);

    if (rc != MDB_SUCCESS) {
        // Nothing is greater or equal than the lower bound, meaning no result
        mdb_cursor_close(cursor);
        return 0;
    }

    currentKey = firstKey;

    // If already bigger than the upper bound
    if (mdb_cmp(d->transaction, d->dbi, &currentKey, &idealLastKey) > 0) {
        mdb_cursor_close(cursor);
        return 0;
    }

    int count = 0;
    do {
        const auto currentBAKey = QByteArray::fromRawData((char *)currentKey.mv_data, currentKey.mv_size);
        const auto currentBAValue = QByteArray::fromRawData((char *)data.mv_data, data.mv_size);
        resultHandler(currentBAKey, currentBAValue);
        count++;
    } while (mdb_cursor_get(cursor, &currentKey, &data, MDB_NEXT) == MDB_SUCCESS &&
             mdb_cmp(d->transaction, d->dbi, &currentKey, &idealLastKey) <= 0);

    mdb_cursor_close(cursor);
    return count;
}

qint64 DataStore::NamedDatabase::getSize()
{
    if (!d || !d->transaction) {
        return -1;
    }

    int rc;
    MDB_stat stat;
    rc = mdb_stat(d->transaction, d->dbi, &stat);
    if (rc) {
        SinkWarning() << "Something went wrong " << QByteArray(mdb_strerror(rc));
    }
    return stat.ms_psize * (stat.ms_leaf_pages + stat.ms_branch_pages + stat.ms_overflow_pages);
}

DataStore::NamedDatabase::Stat DataStore::NamedDatabase::stat()
{
    if (!d || !d->transaction) {
        return {};
    }

    int rc;
    MDB_stat stat;
    rc = mdb_stat(d->transaction, d->dbi, &stat);
    if (rc) {
        SinkWarning() << "Something went wrong " << QByteArray(mdb_strerror(rc));
        return {};
    }
    return {stat.ms_branch_pages,
            stat.ms_leaf_pages,
            stat.ms_overflow_pages,
            stat.ms_entries};
    // std::cout << "page size: " << stat.ms_psize << std::endl;
    // std::cout << "leaf_pages: " << stat.ms_leaf_pages << std::endl;
    // std::cout << "branch_pages: " << stat.ms_branch_pages << std::endl;
    // std::cout << "overflow_pages: " << stat.ms_overflow_pages << std::endl;
    // std::cout << "depth: " << stat.ms_depth << std::endl;
    // std::cout << "entries: " << stat.ms_entries << std::endl;
}

bool DataStore::NamedDatabase::allowsDuplicates() const
{
    unsigned int flags;
    mdb_dbi_flags(d->transaction, d->dbi, &flags);
    return flags & MDB_DUPSORT;
}


class DataStore::Transaction::Private
{
public:
    Private(bool _requestRead, const std::function<void(const DataStore::Error &error)> &_defaultErrorHandler, const QString &_name, MDB_env *_env)
        : env(_env), transaction(nullptr), requestedRead(_requestRead), defaultErrorHandler(_defaultErrorHandler), name(_name), implicitCommit(false), error(false)
    {
    }
    ~Private()
    {
    }

    MDB_env *env;
    MDB_txn *transaction;
    bool requestedRead;
    std::function<void(const DataStore::Error &error)> defaultErrorHandler;
    QString name;
    bool implicitCommit;
    bool error;
    QMap<QString, MDB_dbi> createdDbs;

    bool startTransaction()
    {
        Q_ASSERT(!transaction);
        Q_ASSERT(sEnvironments.values().contains(env));
        Q_ASSERT(env);
        // auto f = [](const char *msg, void *ctx) -> int {
        //     qDebug() << msg;
        //     return 0;
        // };
        // mdb_reader_list(env, f, nullptr);
        // Trace_area("storage." + name.toLatin1()) << "Opening transaction " << requestedRead;
        const int rc = mdb_txn_begin(env, NULL, requestedRead ? MDB_RDONLY : 0, &transaction);
        // Trace_area("storage." + name.toLatin1()) << "Started transaction " << mdb_txn_id(transaction) << transaction;
        if (rc) {
            unsigned int flags;
            mdb_env_get_flags(env, &flags);
            if (flags & MDB_RDONLY && !requestedRead) {
                SinkError() << "Tried to open a write transation in a read-only enironment";
            }
            defaultErrorHandler(Error(name.toLatin1(), ErrorCodes::GenericError, "Error while opening transaction: " + QByteArray(mdb_strerror(rc))));
            return false;
        }
        return true;
    }
};

DataStore::Transaction::Transaction() : d(nullptr)
{
}

DataStore::Transaction::Transaction(Transaction::Private *prv) : d(prv)
{
    if (!d->startTransaction()) {
        delete d;
        d = nullptr;
    }
}

DataStore::Transaction::Transaction(Transaction &&other) : d(nullptr)
{
    *this = std::move(other);
}

DataStore::Transaction &DataStore::Transaction::operator=(DataStore::Transaction &&other)
{
    if (&other != this) {
        abort();
        delete d;
        d = other.d;
        other.d = nullptr;
    }
    return *this;
}

DataStore::Transaction::~Transaction()
{
    if (d && d->transaction) {
        if (d->implicitCommit && !d->error) {
            commit();
        } else {
            // Trace_area("storage." + d->name.toLatin1()) << "Aborting transaction" << mdb_txn_id(d->transaction) << d->transaction;
            abort();
        }
    }
    delete d;
}

DataStore::Transaction::operator bool() const
{
    return (d && d->transaction);
}

bool DataStore::Transaction::commit(const std::function<void(const DataStore::Error &error)> &errorHandler)
{
    if (!d || !d->transaction) {
        return false;
    }

    // Trace_area("storage." + d->name.toLatin1()) << "Committing transaction" << mdb_txn_id(d->transaction) << d->transaction;
    Q_ASSERT(sEnvironments.values().contains(d->env));
    const int rc = mdb_txn_commit(d->transaction);
    if (rc) {
        abort();
        Error error(d->name.toLatin1(), ErrorCodes::TransactionError, "Error during transaction commit: " + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
        //If transactions start failing we're in an unrecoverable situation (i.e. out of diskspace). So throw an exception that will terminate the application.
        throw std::runtime_error("Fatal error while committing transaction.");
    }

    //Add the created dbis to the shared environment
    if (!d->createdDbs.isEmpty()) {
        sDbisLock.lockForWrite();
        for (auto it = d->createdDbs.constBegin(); it != d->createdDbs.constEnd(); it++) {
            //This means we opened the dbi again in a read-only transaction while the write transaction was ongoing.
            Q_ASSERT(!sDbis.contains(it.key()));
            if (!sDbis.contains(it.key())) {
                sDbis.insert(it.key(), it.value());
            }
        }
        d->createdDbs.clear();
        sDbisLock.unlock();
    }

    d->transaction = nullptr;
    return !rc;
}

void DataStore::Transaction::abort()
{
    if (!d || !d->transaction) {
        return;
    }

    // Trace_area("storage." + d->name.toLatin1()) << "Aborting transaction" << mdb_txn_id(d->transaction) << d->transaction;
    Q_ASSERT(sEnvironments.values().contains(d->env));
    mdb_txn_abort(d->transaction);
    d->createdDbs.clear();
    d->transaction = nullptr;
}

DataStore::NamedDatabase DataStore::Transaction::openDatabase(const QByteArray &db,
    const std::function<void(const DataStore::Error &error)> &errorHandler, int flags) const
{
    if (!d) {
        SinkError() << "Tried to open database on invalid transaction: " << db;
        return DataStore::NamedDatabase();
    }
    Q_ASSERT(d->transaction);
    // We don't now if anything changed
    d->implicitCommit = true;
    auto p = new DataStore::NamedDatabase::Private(
        db, flags, d->defaultErrorHandler, d->name, d->transaction);
    auto ret = p->openDatabase(d->requestedRead, errorHandler);
    if (!ret) {
        delete p;
        return DataStore::NamedDatabase();
    }

    if (p->createdNewDbi) {
        d->createdDbs.insert(p->createdNewDbiName, p->dbi);
    }

    auto database = DataStore::NamedDatabase(p);
    return database;
}

QList<QByteArray> DataStore::Transaction::getDatabaseNames() const
{
    if (!d) {
        SinkWarning() << "Invalid transaction";
        return QList<QByteArray>();
    }
    return Sink::Storage::getDatabaseNames(d->transaction);

}


DataStore::Transaction::Stat DataStore::Transaction::stat(bool printDetails)
{
    const int freeDbi = 0;
    const int mainDbi = 1;

	MDB_envinfo mei;
    mdb_env_info(d->env, &mei);

    MDB_stat mst;
    mdb_stat(d->transaction, freeDbi, &mst);
    auto freeStat = NamedDatabase::Stat{mst.ms_branch_pages,
            mst.ms_leaf_pages,
            mst.ms_overflow_pages,
            mst.ms_entries};

    mdb_stat(d->transaction, mainDbi, &mst);
    auto mainStat = NamedDatabase::Stat{mst.ms_branch_pages,
            mst.ms_leaf_pages,
            mst.ms_overflow_pages,
            mst.ms_entries};

    MDB_cursor *cursor;
    MDB_val key, data;
    size_t freePages = 0, *iptr;

    int rc = mdb_cursor_open(d->transaction, freeDbi, &cursor);
    if (rc) {
        fprintf(stderr, "mdb_cursor_open failed, error %d %s\n", rc, mdb_strerror(rc));
        return {};
    }

    while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
        iptr = static_cast<size_t*>(data.mv_data);
        freePages += *iptr;
        bool bad = false;
        size_t pg, prev;
        ssize_t i, j, span = 0;
        j = *iptr++;
        for (i = j, prev = 1; --i >= 0; ) {
            pg = iptr[i];
            if (pg <= prev) {
                bad = true;
            }
            prev = pg;
            pg += span;
            for (; i >= span && iptr[i-span] == pg; span++, pg++) ;
        }
        if (printDetails) {
            std::cout << "    Transaction " << *(size_t *)key.mv_data << ", "<< j << " pages, maxspan " << span << (bad ? " [bad sequence]" : "") << std::endl;
            for (--j; j >= 0; ) {
                pg = iptr[j];
                for (span=1; --j >= 0 && iptr[j] == pg+span; span++);
                if (span > 1) {
                    std::cout << "     " << pg << "[" << span << "]\n";
                } else {
                    std::cout << "     " << pg << std::endl;
                }
            }
        }
    }
    mdb_cursor_close(cursor);
    return {mei.me_last_pgno + 1, freePages, mst.ms_psize, mainStat, freeStat};
}

static size_t mapsize()
{
    if (RUNNING_ON_VALGRIND) {
        // In order to run valgrind this size must be smaller than half your available RAM
        // https://github.com/BVLC/caffe/issues/2404
        return (size_t)1048576 * (size_t)1000; // 1MB * 1000
    }
#ifdef Q_OS_WIN
    //Windows home 10 has a virtual address space limit of 128GB(https://msdn.microsoft.com/en-us/library/windows/desktop/aa366778(v=vs.85).aspx#physical_memory_limits_windows_10). I seems like the 128GB need to accomodate all databases we open in the process.
    return (size_t)1048576 * (size_t)200; // 1MB * 200
#else
    //This is the maximum size of the db (but will not be used directly), so we make it large enough that we hopefully never run into the limit.
    return (size_t)1048576 * (size_t)100000; // 1MB * 100'000
#endif
}

class DataStore::Private
{
public:
    Private(const QString &s, const QString &n, AccessMode m, const DbLayout &layout = {});
    ~Private();

    QString storageRoot;
    QString name;

    MDB_env *env = nullptr;
    AccessMode mode;
    Sink::Log::Context logCtx;

    void initEnvironment(const QString &fullPath, const DbLayout &layout)
    {
        // Ensure the environment is only created once, and that we only have one environment per process
        QReadLocker locker(&sEnvironmentsLock);
        if (!(env = sEnvironments.value(fullPath))) {
            locker.unlock();
            QWriteLocker envLocker(&sEnvironmentsLock);
            QWriteLocker dbiLocker(&sDbisLock);
            if (!(env = sEnvironments.value(fullPath))) {
                int rc = 0;
                if ((rc = mdb_env_create(&env))) {
                    SinkErrorCtx(logCtx) << "mdb_env_create: " << rc << " " << mdb_strerror(rc);
                    env = nullptr;
                    throw std::runtime_error("Fatal error while creating db.");
                } else {
                    //Limit large enough to accomodate all our named dbs. This only starts to matter if the number gets large, otherwise it's just a bunch of extra entries in the main table.
                    mdb_env_set_maxdbs(env, 50);
                    if (const int rc = mdb_env_set_mapsize(env, mapsize())) {
                        SinkErrorCtx(logCtx) << "mdb_env_set_mapsize: " << rc << ":" << mdb_strerror(rc);
                        Q_ASSERT(false);
                        throw std::runtime_error("Fatal error while creating db.");
                    }
                    const bool readOnly = (mode == ReadOnly);
                    unsigned int flags = MDB_NOTLS;
                    if (readOnly) {
                        flags |= MDB_RDONLY;
                    }
                    if ((rc = mdb_env_open(env, fullPath.toStdString().data(), flags, 0664))) {
                        if (readOnly) {
                            SinkLogCtx(logCtx) << "Tried to open non-existing db: " << fullPath;
                        } else {
                            SinkErrorCtx(logCtx) << "mdb_env_open: " << rc << ":" << mdb_strerror(rc);
                            Q_ASSERT(false);
                            throw std::runtime_error("Fatal error while creating db.");
                        }
                        mdb_env_close(env);
                        env = 0;
                    } else {
                        Q_ASSERT(env);
                        sEnvironments.insert(fullPath, env);
                        //Open all available dbi's
                        MDB_txn *transaction;
                        if (const int rc = mdb_txn_begin(env, nullptr, readOnly ? MDB_RDONLY : 0, &transaction)) {
                            SinkWarning() << "Failed to to open transaction: " << QByteArray(mdb_strerror(rc)) << readOnly << transaction;
                            return;
                        }
                        if (!layout.tables.isEmpty()) {

                            //TODO upgrade db if the layout has changed:
                            //* read existing layout
                            //* if layout is not the same create new layout

                            //Create dbis from the given layout.
                            for (auto it = layout.tables.constBegin(); it != layout.tables.constEnd(); it++) {
                                const int flags = it.value();
                                MDB_dbi dbi = 0;
                                const auto &db = it.key();
                                const auto dbiName = name + db;
                                if (createDbi(transaction, db, readOnly, flags, dbi)) {
                                    sDbis.insert(dbiName, dbi);
                                }
                            }
                        } else {
                            //Open all available databases
                            for (const auto &db : getDatabaseNames(transaction)) {
                                MDB_dbi dbi = 0;
                                const auto dbiName = name + db;
                                //We're going to load the flags anyways.
                                const int flags = 0;
                                if (createDbi(transaction, db, readOnly, flags, dbi)) {
                                    sDbis.insert(dbiName, dbi);
                                }
                            }
                        }
                        //To persist the dbis (this is also necessary for read-only transactions)
                        mdb_txn_commit(transaction);
                    }
                }
            }
        }
    }

};

DataStore::Private::Private(const QString &s, const QString &n, AccessMode m, const DbLayout &layout) : storageRoot(s), name(n), env(0), mode(m), logCtx(n.toLatin1())
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
        initEnvironment(fullPath, layout);
    }
}

DataStore::Private::~Private()
{
    //We never close the environment (unless we remove the db), since we should only open the environment once per process (as per lmdb docs)
    //and create storage instance from all over the place. Thus, we're not closing it here on purpose.
}

DataStore::DataStore(const QString &storageRoot, const QString &name, AccessMode mode) : d(new Private(storageRoot, name, mode))
{
}

DataStore::DataStore(const QString &storageRoot, const DbLayout &dbLayout, AccessMode mode) : d(new Private(storageRoot, dbLayout.name, mode, dbLayout))
{
}

DataStore::~DataStore()
{
    delete d;
}

bool DataStore::exists(const QString &storageRoot, const QString &name)
{
    return QFileInfo(storageRoot + '/' + name + "/data.mdb").exists();
}

bool DataStore::exists() const
{
    return (d->env != 0) && DataStore::exists(d->storageRoot, d->name);
}

DataStore::Transaction DataStore::createTransaction(AccessMode type, const std::function<void(const DataStore::Error &error)> &errorHandlerArg)
{
    auto errorHandler = errorHandlerArg ? errorHandlerArg : defaultErrorHandler();
    if (!d->env) {
        errorHandler(Error(d->name.toLatin1(), ErrorCodes::GenericError, "Failed to create transaction: Missing database environment"));
        return Transaction();
    }

    bool requestedRead = type == ReadOnly;

    if (d->mode == ReadOnly && !requestedRead) {
        errorHandler(Error(d->name.toLatin1(), ErrorCodes::GenericError, "Failed to create transaction: Requested read/write transaction in read-only mode."));
        return Transaction();
    }
    QReadLocker locker(&sEnvironmentsLock);
    if (!sEnvironments.values().contains(d->env)) {
        return {};
    }
    return Transaction(new Transaction::Private(requestedRead, defaultErrorHandler(), d->name, d->env));
}

qint64 DataStore::diskUsage() const
{
    QFileInfo info(d->storageRoot + '/' + d->name + "/data.mdb");
    if (!info.exists()) {
        SinkWarning() << "Tried to get filesize for non-existant file: " << info.path();
    }
    return info.size();
}

void DataStore::removeFromDisk() const
{
    const QString fullPath(d->storageRoot + '/' + d->name);
    QWriteLocker dbiLocker(&sDbisLock);
    QWriteLocker envLocker(&sEnvironmentsLock);
    SinkTrace() << "Removing database from disk: " << fullPath;
    auto env = sEnvironments.take(fullPath);
    for (const auto &key : sDbis.keys()) {
        if (key.startsWith(d->name)) {
            sDbis.remove(key);
        }
    }
    mdb_env_close(env);
    QDir dir(fullPath);
    if (!dir.removeRecursively()) {
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, QString("Failed to remove directory %1 %2").arg(d->storageRoot).arg(d->name).toLatin1());
        defaultErrorHandler()(error);
    }
}

void DataStore::clearEnv()
{
    SinkTrace() << "Clearing environment";
    QWriteLocker locker(&sEnvironmentsLock);
    QWriteLocker dbiLocker(&sDbisLock);
    for (const auto &envName : sEnvironments.keys()) {
        auto env = sEnvironments.value(envName);
        mdb_env_sync(env, true);
        for (const auto &k : sDbis.keys()) {
            if (k.startsWith(envName)) {
                auto dbi = sDbis.value(k);
                mdb_dbi_close(env, dbi);
            }
        }
        mdb_env_close(env);
    }
    sDbis.clear();
    sEnvironments.clear();
}

}
} // namespace Sink
