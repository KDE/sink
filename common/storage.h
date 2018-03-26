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

#pragma once

#include "sink_export.h"
#include <string>
#include <functional>
#include <QString>
#include <QMap>

namespace Sink {
namespace Storage {

struct SINK_EXPORT DbLayout {
    typedef QMap<QByteArray, int> Databases;
    DbLayout();
    DbLayout(const QByteArray &, const Databases &);
    QByteArray name;
    Databases tables;
};

class SINK_EXPORT DataStore
{
public:
    enum AccessMode
    {
        ReadOnly,
        ReadWrite
    };

    enum ErrorCodes
    {
        GenericError,
        NotOpen,
        ReadOnlyError,
        TransactionError,
        NotFound
    };

    class Error
    {
    public:
        Error(const QByteArray &s, int c, const QByteArray &m) : store(s), message(m), code(c)
        {
        }
        QByteArray store;
        QByteArray message;
        int code;
    };

    class Transaction;
    class NamedDatabase
    {
    public:
        NamedDatabase();
        ~NamedDatabase();
        /**
         * Write a value
         */
        bool write(const QByteArray &key, const QByteArray &value, const std::function<void(const DataStore::Error &error)> &errorHandler = std::function<void(const DataStore::Error &error)>());

        /**
         * Remove a key
         */
        void remove(const QByteArray &key, const std::function<void(const DataStore::Error &error)> &errorHandler = std::function<void(const DataStore::Error &error)>());
        /**
         * Remove a key-value pair
         */
        void remove(const QByteArray &key, const QByteArray &value, const std::function<void(const DataStore::Error &error)> &errorHandler = std::function<void(const DataStore::Error &error)>());

        /**
        * Read values with a given key.
        *
        * * An empty @param key results in a full scan
        * * If duplicates are existing (revisions), all values are returned.
        * * The pointers of the returned values are valid during the execution of the @param resultHandler
        *
        * @return The number of values retrieved.
        */
        int scan(const QByteArray &key, const std::function<bool(const QByteArray &key, const QByteArray &value)> &resultHandler,
            const std::function<void(const DataStore::Error &error)> &errorHandler = std::function<void(const DataStore::Error &error)>(), bool findSubstringKeys = false, bool skipInternalKeys = true) const;

        /**
         * Finds the last value in a series matched by prefix.
         *
         * This is used to match by uid prefix and find the highest revision.
         * Note that this relies on a key scheme like $uid$revision.
         */
        void findLatest(const QByteArray &uid, const std::function<void(const QByteArray &key, const QByteArray &value)> &resultHandler,
            const std::function<void(const DataStore::Error &error)> &errorHandler = std::function<void(const DataStore::Error &error)>()) const;

        /**
         * Returns true if the database contains the substring key.
         */
        bool contains(const QByteArray &uid);

        NamedDatabase(NamedDatabase &&other);
        NamedDatabase &operator=(NamedDatabase &&other);

        operator bool() const
        {
            return (d != nullptr);
        }

        qint64 getSize();

        struct Stat {
            size_t branchPages;
            size_t leafPages;
            size_t overflowPages;
            size_t numEntries;
        };
        Stat stat();

        bool allowsDuplicates() const;

    private:
        friend Transaction;
        NamedDatabase(NamedDatabase &other);
        NamedDatabase &operator=(NamedDatabase &other);
        class Private;
        NamedDatabase(Private *);
        Private *d;
    };

    class Transaction
    {
    public:
        Transaction();
        ~Transaction();
        bool commit(const std::function<void(const DataStore::Error &error)> &errorHandler = {});
        void abort();

        QList<QByteArray> getDatabaseNames() const;

        NamedDatabase openDatabase(const QByteArray &name = {"default"},
            const std::function<void(const DataStore::Error &error)> &errorHandler = {}, bool allowDuplicates = false) const;

        Transaction(Transaction &&other);
        Transaction &operator=(Transaction &&other);

        operator bool() const;

        struct Stat {
            size_t totalPages;
            size_t freePages;
            size_t pageSize;
            NamedDatabase::Stat mainDbStat;
            NamedDatabase::Stat freeDbStat;
        };
        Stat stat(bool printDetails = true);

    private:
        Transaction(Transaction &other);
        Transaction &operator=(Transaction &other);
        friend DataStore;
        class Private;
        Transaction(Private *);
        Private *d;
    };

    DataStore(const QString &storageRoot, const QString &name, AccessMode mode = ReadOnly);
    DataStore(const QString &storageRoot, const DbLayout &layout, AccessMode mode = ReadOnly);
    ~DataStore();

    Transaction createTransaction(AccessMode mode = ReadWrite, const std::function<void(const DataStore::Error &error)> &errorHandler = std::function<void(const DataStore::Error &error)>());

    /**
     * Set the default error handler.
     */
    void setDefaultErrorHandler(const std::function<void(const DataStore::Error &error)> &errorHandler);
    std::function<void(const DataStore::Error &error)> defaultErrorHandler() const;

    /**
     * A basic error handler that writes to std::cerr.
     *
     * Used if nothing else is configured.
     */
    static std::function<void(const DataStore::Error &error)> basicErrorHandler();

    qint64 diskUsage() const;
    void removeFromDisk() const;

    /**
     * Clears all cached environments.
     *
     * This only ever has to be called if a database was removed from another process.
     */
    static void clearEnv();

    static qint64 maxRevision(const Transaction &);
    static void setMaxRevision(Transaction &, qint64 revision);

    static qint64 cleanedUpRevision(const Transaction &);
    static void setCleanedUpRevision(Transaction &, qint64 revision);

    static QByteArray getUidFromRevision(const Transaction &, qint64 revision);
    static QByteArray getTypeFromRevision(const Transaction &, qint64 revision);
    static void recordRevision(Transaction &, qint64 revision, const QByteArray &uid, const QByteArray &type);
    static void removeRevision(Transaction &, qint64 revision);
    static void recordUid(DataStore::Transaction &transaction, const QByteArray &uid, const QByteArray &type);
    static void removeUid(DataStore::Transaction &transaction, const QByteArray &uid, const QByteArray &type);
    static void getUids(const QByteArray &type, const Transaction &, const std::function<void(const QByteArray &uid)> &);

    bool exists() const;
    static bool exists(const QString &storageRoot, const QString &name);

    static bool isInternalKey(const char *key);
    static bool isInternalKey(void *key, int keySize);
    static bool isInternalKey(const QByteArray &key);

    static QByteArray assembleKey(const QByteArray &key, qint64 revision);
    static QByteArray uidFromKey(const QByteArray &key);
    static qint64 revisionFromKey(const QByteArray &key);

    static NamedDatabase mainDatabase(const Transaction &, const QByteArray &type);

    static QByteArray generateUid();

    static qint64 databaseVersion(const Transaction &);
    static void setDatabaseVersion(Transaction &, qint64 revision);

private:
    std::function<void(const DataStore::Error &error)> mErrorHandler;

private:
    class Private;
    Private *const d;
};

}
} // namespace Sink

SINK_EXPORT QDebug& operator<<(QDebug &dbg, const Sink::Storage::DataStore::Error &error);
