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
#include <valgrind.h>

#include <lmdb.h>
#include "log.h"

SINK_DEBUG_AREA("storage")
// SINK_DEBUG_COMPONENT(d->storageRoot.toLatin1() + '/' + d->name.toLatin1())

namespace Sink {
namespace Storage {

    //TODO a QReadWriteLock would be a better choice because we mostly do reads.
extern QMutex sMutex;
extern QHash<QString, MDB_env *> sEnvironments;
extern QHash<QString, MDB_dbi> sDbis;


QMutex sMutex;
QHash<QString, MDB_env *> sEnvironments;
QHash<QString, MDB_dbi> sDbis;

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


class DataStore::NamedDatabase::Private
{
public:
    Private(const QByteArray &_db, bool _allowDuplicates, const std::function<void(const DataStore::Error &error)> &_defaultErrorHandler, const QString &_name, MDB_txn *_txn)
        : db(_db), transaction(_txn), allowDuplicates(_allowDuplicates), defaultErrorHandler(_defaultErrorHandler), name(_name)
    {
    }

    ~Private()
    {
    }

    QByteArray db;
    MDB_txn *transaction;
    MDB_dbi dbi;
    bool allowDuplicates;
    std::function<void(const DataStore::Error &error)> defaultErrorHandler;
    QString name;
    bool createdNewDbi = false;
    QString createdDbName;

    bool openDatabase(bool readOnly, std::function<void(const DataStore::Error &error)> errorHandler)
    {
        unsigned int flags = 0;
        if (allowDuplicates) {
            flags |= MDB_DUPSORT;
        }

        const auto dbiName = name + db;
        if (sDbis.contains(dbiName)) {
            dbi = sDbis.value(dbiName);
            //sDbis can contain dbi's that are not available to this transaction.
            //We use mdb_dbi_flags to check if the dbi is valid for this transaction.
            uint f;
            if (mdb_dbi_flags(transaction, dbi, &f) == EINVAL) {
                //In readonly mode we can just ignore this. In read-write we would have tried to concurrently create a db.
                if (!readOnly) {
                    SinkWarning() << "Tried to create database in second transaction: " << dbiName;
                }
                dbi = 0;
                transaction = 0;
                return false;
            }
        } else {
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

            Q_ASSERT(transaction);
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
                        Error error(name.toLatin1(), ErrorCodes::GenericError, "Error while creating database: " + QByteArray(mdb_strerror(rc)));
                        errorHandler ? errorHandler(error) : defaultErrorHandler(error);
                        return false;
                    }
                    //Record the db flags
                    MDB_val key, value;
                    key.mv_data = const_cast<void*>(static_cast<const void*>(db.constData()));
                    key.mv_size = db.size();
                    //Store the flags without the create option
                    const auto ba = QByteArray::number(flags);
                    value.mv_data = const_cast<void*>(static_cast<const void*>(db.constData()));
                    value.mv_size = db.size();
                    if (const int rc = mdb_put(transaction, flagtableDbi, &key, &value, MDB_NOOVERWRITE)) {
                        //We expect this to fail if we're only creating the dbi but not the db
                        if (rc != MDB_KEYEXIST) {
                            SinkWarning() << "Failed to write flags to flag db: " << QByteArray(mdb_strerror(rc));
                        }
                    }
                } else {
                    dbi = 0;
                    transaction = 0;
                    //It's not an error if we only want to read
                    if (!readOnly) {
                        SinkWarning() << "Failed to open db " << QByteArray(mdb_strerror(rc));
                        Error error(name.toLatin1(), ErrorCodes::GenericError, "Error while opening database: " + QByteArray(mdb_strerror(rc)));
                        errorHandler ? errorHandler(error) : defaultErrorHandler(error);
                    }
                    return false;
                }
            }

            createdNewDbi = true;
            createdDbName = dbiName;
        }
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
        Error error(d->name.toLatin1() + d->db, ErrorCodes::GenericError, "mdb_put: " + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    }

    return !rc;
}

void DataStore::NamedDatabase::remove(const QByteArray &k, const std::function<void(const DataStore::Error &error)> &errorHandler)
{
    remove(k, QByteArray(), errorHandler);
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

int DataStore::NamedDatabase::scan(const QByteArray &k, const std::function<bool(const QByteArray &key, const QByteArray &value)> &resultHandler,
    const std::function<void(const DataStore::Error &error)> &errorHandler, bool findSubstringKeys, bool skipInternalKeys) const
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

    if (k.isEmpty() || d->allowDuplicates || findSubstringKeys) {
        MDB_cursor_op op = d->allowDuplicates ? MDB_SET : MDB_FIRST;
        if (findSubstringKeys) {
            op = MDB_SET_RANGE;
        }
        if ((rc = mdb_cursor_get(cursor, &key, &data, op)) == 0) {
            const auto current = QByteArray::fromRawData((char *)key.mv_data, key.mv_size);
            // The first lookup will find a key that is equal or greather than our key
            if (current.startsWith(k)) {
                const bool callResultHandler =  !(skipInternalKeys && isInternalKey(current));
                if (callResultHandler) {
                    numberOfRetrievedValues++;
                }
                if (!callResultHandler || resultHandler(current, QByteArray::fromRawData((char *)data.mv_data, data.mv_size))) {
                    if (findSubstringKeys) {
                        // Reset the key to what we search for
                        key.mv_data = (void *)k.constData();
                        key.mv_size = k.size();
                    }
                    MDB_cursor_op nextOp = (d->allowDuplicates && !findSubstringKeys) ? MDB_NEXT_DUP : MDB_NEXT;
                    while ((rc = mdb_cursor_get(cursor, &key, &data, nextOp)) == 0) {
                        const auto current = QByteArray::fromRawData((char *)key.mv_data, key.mv_size);
                        // Every consequitive lookup simply iterates through the list
                        if (current.startsWith(k)) {
                            const bool callResultHandler =  !(skipInternalKeys && isInternalKey(current));
                            if (callResultHandler) {
                                numberOfRetrievedValues++;
                                if (!resultHandler(current, QByteArray::fromRawData((char *)data.mv_data, data.mv_size))) {
                                    break;
                                }
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
        Error error(d->name.toLatin1() + d->db, getErrorCode(rc), QByteArray("Key: ") + k + " : " + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    }

    return numberOfRetrievedValues;
}

void DataStore::NamedDatabase::findLatest(const QByteArray &k, const std::function<void(const QByteArray &key, const QByteArray &value)> &resultHandler,
    const std::function<void(const DataStore::Error &error)> &errorHandler) const
{
    if (!d || !d->transaction) {
        // Not an error. We rely on this to read nothing from non-existing databases.
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
            bool advanced = false;
            while (QByteArray::fromRawData((char *)key.mv_data, key.mv_size).startsWith(k)) {
                advanced = true;
                MDB_cursor_op nextOp = MDB_NEXT;
                rc = mdb_cursor_get(cursor, &key, &data, nextOp);
                if (rc) {
                    break;
                }
            }
            if (advanced) {
                MDB_cursor_op prefOp = MDB_PREV;
                // We read past the end above, just take the last value
                if (rc == MDB_NOTFOUND) {
                    prefOp = MDB_LAST;
                }
                rc = mdb_cursor_get(cursor, &key, &data, prefOp);
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
        Error error(d->name.toLatin1(), getErrorCode(rc), QByteArray("Key: ") + k + " : " + QByteArray(mdb_strerror(rc)));
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    } else if (!foundValue) {
        Error error(d->name.toLatin1(), 1, QByteArray("Key: ") + k + " : No value found");
        errorHandler ? errorHandler(error) : d->defaultErrorHandler(error);
    }

    return;
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
    // std::cout << "overflow_pages: " << stat.ms_overflow_pages << std::endl;
    // std::cout << "page size: " << stat.ms_psize << std::endl;
    // std::cout << "branch_pages: " << stat.ms_branch_pages << std::endl;
    // std::cout << "leaf_pages: " << stat.ms_leaf_pages << std::endl;
    // std::cout << "depth: " << stat.ms_depth << std::endl;
    // std::cout << "entries: " << stat.ms_entries << std::endl;
    return stat.ms_psize * (stat.ms_leaf_pages + stat.ms_branch_pages + stat.ms_overflow_pages);
}


class DataStore::Transaction::Private
{
public:
    Private(bool _requestRead, const std::function<void(const DataStore::Error &error)> &_defaultErrorHandler, const QString &_name, MDB_env *_env, bool _noLock = false)
        : env(_env), transaction(nullptr), requestedRead(_requestRead), defaultErrorHandler(_defaultErrorHandler), name(_name), implicitCommit(false), error(false), modificationCounter(0), noLock(_noLock)
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
    int modificationCounter;
    bool noLock;

    QMap<QString, MDB_dbi> createdDbs;

    void startTransaction()
    {
        Q_ASSERT(!transaction);
        // auto f = [](const char *msg, void *ctx) -> int {
        //     qDebug() << msg;
        //     return 0;
        // };
        // mdb_reader_list(env, f, nullptr);
        // Trace_area("storage." + name.toLatin1()) << "Opening transaction " << requestedRead;
        const int rc = mdb_txn_begin(env, NULL, requestedRead ? MDB_RDONLY : 0, &transaction);
        // Trace_area("storage." + name.toLatin1()) << "Started transaction " << mdb_txn_id(transaction) << transaction;
        if (rc) {
            defaultErrorHandler(Error(name.toLatin1(), ErrorCodes::GenericError, "Error while opening transaction: " + QByteArray(mdb_strerror(rc))));
        }
    }
};

DataStore::Transaction::Transaction() : d(nullptr)
{
}

DataStore::Transaction::Transaction(Transaction::Private *prv) : d(prv)
{
    d->startTransaction();
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
    d->transaction = nullptr;

    //Add the created dbis to the shared environment
    if (!d->createdDbs.isEmpty()) {
        if (!d->noLock) {
            sMutex.lock();
        }
        for (auto it = d->createdDbs.constBegin(); it != d->createdDbs.constEnd(); it++) {
            Q_ASSERT(!sDbis.contains(it.key()));
            sDbis.insert(it.key(), it.value());
        }
        d->createdDbs.clear();
        if (!d->noLock) {
            sMutex.unlock();
        }
    }

    return !rc;
}

void DataStore::Transaction::abort()
{
    if (!d || !d->transaction) {
        return;
    }

    d->createdDbs.clear();
    // Trace_area("storage." + d->name.toLatin1()) << "Aborting transaction" << mdb_txn_id(d->transaction) << d->transaction;
    Q_ASSERT(sEnvironments.values().contains(d->env));
    mdb_txn_abort(d->transaction);
    d->transaction = nullptr;
}

//Ensure that we opened the correct database by comparing the expected identifier with the one
//we write to the database on first open.
static bool ensureCorrectDb(DataStore::NamedDatabase &database, const QByteArray &db, bool readOnly)
{
    bool openedTheWrongDatabase = false;
    auto count = database.scan("__internal_dbname", [db, &openedTheWrongDatabase](const QByteArray &key, const QByteArray &value) ->bool {
        if (value != db) {
            SinkWarning() << "Opened the wrong database, got " << value << " instead of " << db;
            openedTheWrongDatabase = true;
        }
        return false;
    },
    [&](const DataStore::Error &) {
    }, false);
    //This is the first time we open this database in a write transaction, write the db name
    if (!count) {
        if (!readOnly) {
            database.write("__internal_dbname", db);
        }
    }
    return !openedTheWrongDatabase;
}

DataStore::NamedDatabase DataStore::Transaction::openDatabase(const QByteArray &db, const std::function<void(const DataStore::Error &error)> &errorHandler, bool allowDuplicates) const
{
    if (!d) {
        SinkError() << "Tried to open database on invalid transaction: " << db;
        return DataStore::NamedDatabase();
    }
    Q_ASSERT(d->transaction);
    // We don't now if anything changed
    d->implicitCommit = true;
    auto p = new DataStore::NamedDatabase::Private(db, allowDuplicates, d->defaultErrorHandler, d->name, d->transaction);
    if (!d->noLock) {
        //This wouldn't be necessary with a read-write lock, we could read-only lock here
        sMutex.lock();
    }
    if (!p->openDatabase(d->requestedRead, errorHandler)) {
        if (!d->noLock) {
            sMutex.unlock();
        }
        delete p;
        return DataStore::NamedDatabase();
    }
    if (!d->noLock) {
        sMutex.unlock();
    }
    if (p->createdNewDbi) {
        d->createdDbs.insert(p->createdDbName, p->dbi);
    }
    auto database = DataStore::NamedDatabase(p);
    if (!ensureCorrectDb(database, db, d->requestedRead)) {
        SinkWarning() << "Failed to open the database correctly" << db;
        Q_ASSERT(false);
        return DataStore::NamedDatabase();
    }
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


class DataStore::Private
{
public:
    Private(const QString &s, const QString &n, AccessMode m, const DbLayout &layout = {});
    ~Private();

    QString storageRoot;
    QString name;

    MDB_env *env;
    AccessMode mode;
};

DataStore::Private::Private(const QString &s, const QString &n, AccessMode m, const DbLayout &layout) : storageRoot(s), name(n), env(0), mode(m)
{
    const QString fullPath(storageRoot + '/' + name);
    QFileInfo dirInfo(fullPath);
    if (!dirInfo.exists() && mode == ReadWrite) {
        QDir().mkpath(fullPath);
        dirInfo.refresh();
    }
    Sink::Log::Context logCtx{n.toLatin1()};
    if (mode == ReadWrite && !dirInfo.permission(QFile::WriteOwner)) {
        qCritical() << fullPath << "does not have write permissions. Aborting";
    } else if (dirInfo.exists()) {
        // Ensure the environment is only created once
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
                SinkWarningCtx(logCtx) << "mdb_env_create: " << rc << " " << mdb_strerror(rc);
            } else {
                mdb_env_set_maxdbs(env, 50);
                unsigned int flags = MDB_NOTLS;
                if (mode == ReadOnly) {
                    flags |= MDB_RDONLY;
                }
                if ((rc = mdb_env_open(env, fullPath.toStdString().data(), flags, 0664))) {
                    SinkWarningCtx(logCtx) << "mdb_env_open: " << rc << ":" << mdb_strerror(rc);
                    mdb_env_close(env);
                    env = 0;
                } else {
                    if (RUNNING_ON_VALGRIND) {
                        // In order to run valgrind this size must be smaller than half your available RAM
                        // https://github.com/BVLC/caffe/issues/2404
                        const size_t dbSize = (size_t)10485760 * (size_t)1000; // 1MB * 1000
                        mdb_env_set_mapsize(env, dbSize);
                    } else {
                        // FIXME: dynamic resize
                        const size_t dbSize = (size_t)10485760 * (size_t)8000; // 1MB * 8000
                        mdb_env_set_mapsize(env, dbSize);
                    }
                    sEnvironments.insert(fullPath, env);
                    //Open all available dbi's
                    bool noLock = true;
                    bool requestedRead = m == ReadOnly;
                    auto t = Transaction(new Transaction::Private(requestedRead, nullptr, name, env, noLock));
                    if (!layout.tables.isEmpty()) {
                        for (auto it = layout.tables.constBegin(); it != layout.tables.constEnd(); it++) {
                            bool allowDuplicates = it.value();
                            t.openDatabase(it.key(), {}, allowDuplicates);
                        }
                    } else {
                        for (const auto &db : t.getDatabaseNames()) {
                            //Get dbi to store for future use.
                            t.openDatabase(db);
                        }
                    }
                    //To persist the dbis (this is also necessary for read-only transactions)
                    t.commit();
                }
            }
        }
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

bool DataStore::exists() const
{
    return (d->env != 0);
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
    QMutexLocker locker(&sMutex);
    SinkTrace() << "Removing database from disk: " << fullPath;
    sEnvironments.take(fullPath);
    for (const auto &key : sDbis.keys()) {
        if (key.startsWith(d->name)) {
            sDbis.remove(key);
        }
    }
    auto env = sEnvironments.take(fullPath);
    mdb_env_close(env);
    QDir dir(fullPath);
    if (!dir.removeRecursively()) {
        Error error(d->name.toLatin1(), ErrorCodes::GenericError, QString("Failed to remove directory %1 %2").arg(d->storageRoot).arg(d->name).toLatin1());
        defaultErrorHandler()(error);
    }
}

void DataStore::clearEnv()
{
    QMutexLocker locker(&sMutex);
    for (auto env : sEnvironments) {
        mdb_env_close(env);
    }
    sDbis.clear();
    sEnvironments.clear();
}

}
} // namespace Sink
