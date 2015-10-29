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

#include <akonadi2common_export.h>
#include <string>
#include <functional>
#include <QString>

namespace Akonadi2
{

class AKONADI2COMMON_EXPORT Storage {
public:
    enum AccessMode { ReadOnly, ReadWrite };

    enum ErrorCodes {
        GenericError,
        NotOpen,
        ReadOnlyError,
        TransactionError,
        NotFound
    };

    class Error
    {
    public:
        Error(const QByteArray &s, int c, const QByteArray &m)
            : store(s), message(m), code(c) {}
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
        bool write(const QByteArray &key, const QByteArray &value, const std::function<void(const Storage::Error &error)> &errorHandler = std::function<void(const Storage::Error &error)>());

        /**
         * Remove a key
         */
        void remove(const QByteArray &key,
                    const std::function<void(const Storage::Error &error)> &errorHandler = std::function<void(const Storage::Error &error)>());
        /**
         * Remove a key-value pair
         */
        void remove(const QByteArray &key, const QByteArray &value,
                    const std::function<void(const Storage::Error &error)> &errorHandler = std::function<void(const Storage::Error &error)>());

        /**
        * Read values with a given key.
        *
        * * An empty @param key results in a full scan
        * * If duplicates are existing (revisions), all values are returned.
        * * The pointers of the returned values are valid during the execution of the @param resultHandler
        *
        * @return The number of values retrieved.
        */
        int scan(const QByteArray &key,
                    const std::function<bool(const QByteArray &key, const QByteArray &value)> &resultHandler,
                    const std::function<void(const Storage::Error &error)> &errorHandler = std::function<void(const Storage::Error &error)>(), bool findSubstringKeys = false) const;

        void findLatest(const QByteArray &uid,
                        const std::function<void(const QByteArray &key, const QByteArray &value)> &resultHandler,
                        const std::function<void(const Storage::Error &error)> &errorHandler = std::function<void(const Storage::Error &error)>()) const;

        NamedDatabase(NamedDatabase&& other) : d(other.d)
        {
            d = other.d;
            other.d = nullptr;
        }

        NamedDatabase& operator=(NamedDatabase&& other) {
            d = other.d;
            other.d = nullptr;
            return *this;
        }

        operator bool() const {
            return (d != nullptr);
        }

    private:
        friend Transaction;
        NamedDatabase(NamedDatabase& other);
        NamedDatabase& operator=(NamedDatabase& other);
        class Private;
        NamedDatabase(Private*);
        Private *d;
    };

    class Transaction
    {
    public:
        Transaction();
        ~Transaction();
        bool commit(const std::function<void(const Storage::Error &error)> &errorHandler = std::function<void(const Storage::Error &error)>());
        void abort();

        NamedDatabase openDatabase(const QByteArray &name = QByteArray("default"), const std::function<void(const Storage::Error &error)> &errorHandler = std::function<void(const Storage::Error &error)>(), bool allowDuplicates = false) const;

        Transaction(Transaction&& other) : d(other.d)
        {
            d = other.d;
            other.d = nullptr;
        }
        Transaction& operator=(Transaction&& other) {
            d = other.d;
            other.d = nullptr;
            return *this;
        }

        operator bool() const {
            return (d != nullptr);
        }

    private:
        Transaction(Transaction& other);
        Transaction& operator=(Transaction& other);
        friend Storage;
        class Private;
        Transaction(Private*);
        Private *d;
    };

    Storage(const QString &storageRoot, const QString &name, AccessMode mode = ReadOnly);
    ~Storage();

    Transaction createTransaction(AccessMode mode = ReadWrite,
                  const std::function<void(const Storage::Error &error)> &errorHandler = std::function<void(const Storage::Error &error)>());

    /**
     * Set the default error handler.
     */
    void setDefaultErrorHandler(const std::function<void(const Storage::Error &error)> &errorHandler);
    std::function<void(const Storage::Error &error)> defaultErrorHandler() const;

    /**
     * A basic error handler that writes to std::cerr.
     * 
     * Used if nothing else is configured.
     */
    static std::function<void(const Storage::Error &error)> basicErrorHandler();

    qint64 diskUsage() const;
    void removeFromDisk() const;

    static qint64 maxRevision(const Akonadi2::Storage::Transaction &);
    static void setMaxRevision(Akonadi2::Storage::Transaction &, qint64 revision);

    static qint64 cleanedUpRevision(const Akonadi2::Storage::Transaction &);
    static void setCleanedUpRevision(Akonadi2::Storage::Transaction &, qint64 revision);

    static QByteArray getUidFromRevision(const Akonadi2::Storage::Transaction &, qint64 revision);
    static QByteArray getTypeFromRevision(const Akonadi2::Storage::Transaction &, qint64 revision);
    static void recordRevision(Akonadi2::Storage::Transaction &, qint64 revision, const QByteArray &uid, const QByteArray &type);
    static void removeRevision(Akonadi2::Storage::Transaction &, qint64 revision);

    bool exists() const;

    static bool isInternalKey(const char *key);
    static bool isInternalKey(void *key, int keySize);
    static bool isInternalKey(const QByteArray &key);

    static QByteArray assembleKey(const QByteArray &key, qint64 revision);
    static QByteArray uidFromKey(const QByteArray &key);

private:
    std::function<void(const Storage::Error &error)> mErrorHandler;

private:
    class Private;
    Private * const d;
};

} // namespace Akonadi2

