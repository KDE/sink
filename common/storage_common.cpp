/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 * Copyright (C) 2014 Christian Mollekopf <mollekopf@kolabsys.com>
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

#include "log.h"
#include "utils.h"

QDebug& operator<<(QDebug &dbg, const Sink::Storage::DataStore::Error &error)
{
    dbg << error.message << "Code: " << error.code << "Db: " << error.store;
    return dbg;
}

namespace Sink {
namespace Storage {

static const char *s_internalPrefix = "__internal";
static const int s_internalPrefixSize = strlen(s_internalPrefix);
static const int s_lengthOfUid = 38;

DbLayout::DbLayout()
{

}

DbLayout::DbLayout(const QByteArray &n, const Databases &t)
    : name(n),
    tables(t)
{

}

void errorHandler(const DataStore::Error &error)
{
    if (error.code == DataStore::TransactionError) {
        SinkError() << "Database error in " << error.store << ", code " << error.code << ", message: " << error.message;
    } else {
        SinkWarning() << "Database error in " << error.store << ", code " << error.code << ", message: " << error.message;
    }
}

std::function<void(const DataStore::Error &error)> DataStore::basicErrorHandler()
{
    return errorHandler;
}

void DataStore::setDefaultErrorHandler(const std::function<void(const DataStore::Error &error)> &errorHandler)
{
    mErrorHandler = errorHandler;
}

std::function<void(const DataStore::Error &error)> DataStore::defaultErrorHandler() const
{
    if (mErrorHandler) {
        return mErrorHandler;
    }
    return basicErrorHandler();
}

void DataStore::setMaxRevision(DataStore::Transaction &transaction, qint64 revision)
{
    transaction.openDatabase().write("__internal_maxRevision", QByteArray::number(revision));
}

qint64 DataStore::maxRevision(const DataStore::Transaction &transaction)
{
    qint64 r = 0;
    transaction.openDatabase().scan("__internal_maxRevision",
        [&](const QByteArray &, const QByteArray &revision) -> bool {
            r = revision.toLongLong();
            return false;
        },
        [](const Error &error) {
            if (error.code != DataStore::NotFound) {
                SinkWarning() << "Couldn't find the maximum revision: " << error;
            }
        });
    return r;
}

void DataStore::setCleanedUpRevision(DataStore::Transaction &transaction, qint64 revision)
{
    transaction.openDatabase().write("__internal_cleanedUpRevision", QByteArray::number(revision));
}

qint64 DataStore::cleanedUpRevision(const DataStore::Transaction &transaction)
{
    qint64 r = 0;
    transaction.openDatabase().scan("__internal_cleanedUpRevision",
        [&](const QByteArray &, const QByteArray &revision) -> bool {
            r = revision.toLongLong();
            return false;
        },
        [](const Error &error) {
            if (error.code != DataStore::NotFound) {
                SinkWarning() << "Couldn't find the cleanedUpRevision: " << error;
            }
        });
    return r;
}

QByteArray DataStore::getUidFromRevision(const DataStore::Transaction &transaction, qint64 revision)
{
    QByteArray uid;
    transaction.openDatabase("revisions")
        .scan(QByteArray::number(revision),
            [&](const QByteArray &, const QByteArray &value) -> bool {
                uid = QByteArray{value.constData(), value.size()};
                return false;
            },
            [revision](const Error &error) { SinkWarning() << "Couldn't find uid for revision: " << revision << error.message; });
    return uid;
}

QByteArray DataStore::getTypeFromRevision(const DataStore::Transaction &transaction, qint64 revision)
{
    QByteArray type;
    transaction.openDatabase("revisionType")
        .scan(QByteArray::number(revision),
            [&](const QByteArray &, const QByteArray &value) -> bool {
                type = QByteArray{value.constData(), value.size()};
                return false;
            },
            [revision](const Error &error) { SinkWarning() << "Couldn't find type for revision " << revision; });
    return type;
}

void DataStore::recordRevision(DataStore::Transaction &transaction, qint64 revision, const QByteArray &uid, const QByteArray &type)
{
    // TODO use integerkeys
    transaction.openDatabase("revisions").write(QByteArray::number(revision), uid);
    transaction.openDatabase("revisionType").write(QByteArray::number(revision), type);
}

void DataStore::removeRevision(DataStore::Transaction &transaction, qint64 revision)
{
    transaction.openDatabase("revisions").remove(QByteArray::number(revision));
    transaction.openDatabase("revisionType").remove(QByteArray::number(revision));
}

void DataStore::recordUid(DataStore::Transaction &transaction, const QByteArray &uid, const QByteArray &type)
{
    transaction.openDatabase(type + "uids").write(uid, "");
}

void DataStore::removeUid(DataStore::Transaction &transaction, const QByteArray &uid, const QByteArray &type)
{
    transaction.openDatabase(type + "uids").remove(uid);
}

void DataStore::getUids(const QByteArray &type, const Transaction &transaction, const std::function<void(const QByteArray &uid)> &callback)
{
    transaction.openDatabase(type + "uids").scan("", [&] (const QByteArray &key, const QByteArray &) {
        callback(key);
        return true;
    });
}

bool DataStore::isInternalKey(const char *key)
{
    return key && strncmp(key, s_internalPrefix, s_internalPrefixSize) == 0;
}

bool DataStore::isInternalKey(void *key, int size)
{
    if (size < 1) {
        return false;
    }

    return key && strncmp(static_cast<char *>(key), s_internalPrefix, (size > s_internalPrefixSize ? s_internalPrefixSize : size)) == 0;
}

bool DataStore::isInternalKey(const QByteArray &key)
{
    return key.startsWith(s_internalPrefix);
}

QByteArray DataStore::assembleKey(const QByteArray &key, qint64 revision)
{
    Q_ASSERT(revision <= 9223372036854775807);
    Q_ASSERT(key.size() == s_lengthOfUid);
    return key + QByteArray::number(revision).rightJustified(19, '0', false);
}

QByteArray DataStore::uidFromKey(const QByteArray &key)
{
    return key.mid(0, s_lengthOfUid);
}

qint64 DataStore::revisionFromKey(const QByteArray &key)
{
    return key.mid(s_lengthOfUid + 1).toLongLong();
}

QByteArray DataStore::generateUid()
{
    return createUuid();
}

DataStore::NamedDatabase DataStore::mainDatabase(const DataStore::Transaction &t, const QByteArray &type)
{
    if (type.isEmpty()) {
        SinkError() << "Tried to open main database for empty type.";
        Q_ASSERT(false);
        return {};
    }
    return t.openDatabase(type + ".main");
}

bool DataStore::NamedDatabase::contains(const QByteArray &uid)
{
    bool found = false;
    scan(uid,
        [&found](const QByteArray &, const QByteArray &) -> bool {
            found = true;
            return false;
        },
        [](const DataStore::Error &error) {}, true);
    return found;
}

void DataStore::setDatabaseVersion(DataStore::Transaction &transaction, qint64 revision)
{
    transaction.openDatabase().write("__internal_databaseVersion", QByteArray::number(revision));
}

qint64 DataStore::databaseVersion(const DataStore::Transaction &transaction)
{
    qint64 r = 0;
    transaction.openDatabase().scan("__internal_databaseVersion",
        [&](const QByteArray &, const QByteArray &revision) -> bool {
            r = revision.toLongLong();
            return false;
        },
        [](const Error &error) {
            if (error.code != DataStore::NotFound) {
                SinkWarning() << "Couldn't find the database version: " << error;
            }
        });
    return r;
}


}
} // namespace Sink
