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

    class Error
    {
    public:
        Error(const std::string &s, int c, const std::string &m)
            : store(s), message(m), code(c) {}
        std::string store;
        std::string message;
        int code;
    };

    Storage(const QString &storageRoot, const QString &name, AccessMode mode = ReadOnly);
    ~Storage();
    bool isInTransaction() const;
    bool startTransaction(AccessMode mode = ReadWrite);
    bool commitTransaction();
    void abortTransaction();
    //TODO: row removal
    //TODO: cursor based read
    //TODO: query?
    bool write(const char *key, size_t keySize, const char *value, size_t valueSize);
    bool write(const std::string &sKey, const std::string &sValue);
    void read(const std::string &sKey,
              const std::function<bool(const std::string &value)> &resultHandler);
    void read(const std::string &sKey,
              const std::function<bool(const std::string &value)> &resultHandler,
              const std::function<void(const Storage::Error &error)> &errorHandler);
    void read(const std::string &sKey, const std::function<bool(void *ptr, int size)> &resultHandler);
    void read(const std::string &sKey,
              const std::function<bool(void *ptr, int size)> & resultHandler,
              const std::function<void(const Storage::Error &error)> &errorHandler);
    void scan(const std::string &sKey, const std::function<bool(void *keyPtr, int keySize, void *valuePtr, int valueSize)> &resultHandler);
    void scan(const char *keyData, uint keySize,
              const std::function<bool(void *keyPtr, int keySize, void *ptr, int size)> &resultHandler,
              const std::function<void(const Storage::Error &error)> &errorHandler);

    static std::function<void(const Storage::Error &error)> basicErrorHandler();
    qint64 diskUsage() const;
    void removeFromDisk() const;

    qint64 maxRevision();
    void setMaxRevision(qint64 revision);

    bool exists() const;
private:
    class Private;
    Private * const d;
};

} // namespace Akonadi2

