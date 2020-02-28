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

QMap<QByteArray, int> DataStore::baseDbs()
{
    return {{"revisionType", Storage::IntegerKeys},
            {"revisions", Storage::IntegerKeys},
            {"uidsToRevisions", Storage::AllowDuplicates | Storage::IntegerValues},
            {"default", 0},
            {"__metadata", 0},
            {"__flagtable", 0}};
}

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
        SinkError() << "Transaction error:" << error;
    } else {
        SinkWarning() << "Database error:" << error;
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
    transaction.openDatabase("__metadata").write("maxRevision", QByteArray::number(revision));
}

qint64 DataStore::maxRevision(const DataStore::Transaction &transaction)
{
    qint64 r = 0;
    transaction.openDatabase("__metadata").scan("maxRevision",
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
    transaction.openDatabase("__metadata").write("cleanedUpRevision", QByteArray::number(revision));
}

qint64 DataStore::cleanedUpRevision(const DataStore::Transaction &transaction)
{
    qint64 r = 0;
    transaction.openDatabase("__metadata").scan("cleanedUpRevision",
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

Identifier DataStore::getUidFromRevision(const DataStore::Transaction &transaction, size_t revision)
{
    QByteArray uid;
    transaction
        .openDatabase("revisions", /* errorHandler = */ {}, IntegerKeys)
        .scan(revision,
            [&](const size_t, const QByteArray &value) -> bool {
                uid = QByteArray{ value.constData(), value.size() };
                return false;
            },
            [revision](const Error &error) {
                SinkWarning() << "Couldn't find uid for revision: " << revision << error.message;
            });
    Q_ASSERT(!uid.isEmpty());
    return Identifier::fromInternalByteArray(uid);
}

size_t DataStore::getLatestRevisionFromUid(DataStore::Transaction &t, const Identifier &uid)
{
    size_t revision = 0;
    t.openDatabase("uidsToRevisions", {}, AllowDuplicates | IntegerValues)
        .findLatest(uid.toInternalByteArray(), [&revision](const QByteArray &key, const QByteArray &value) {
            revision = byteArrayToSizeT(value);
        });

    return revision;
}

QList<size_t> DataStore::getRevisionsUntilFromUid(DataStore::Transaction &t, const Identifier &uid, size_t lastRevision)
{
    QList<size_t> queriedRevisions;
    t.openDatabase("uidsToRevisions", {}, AllowDuplicates | IntegerValues)
        .scan(uid.toInternalByteArray(), [&queriedRevisions, lastRevision](const QByteArray &, const QByteArray &value) {
            size_t currentRevision = byteArrayToSizeT(value);
            if (currentRevision < lastRevision) {
                queriedRevisions << currentRevision;
                return true;
            }

            return false;
        });

    return queriedRevisions;
}

QList<size_t> DataStore::getRevisionsFromUid(DataStore::Transaction &t, const Identifier &uid)
{
    QList<size_t> queriedRevisions;
    t.openDatabase("uidsToRevisions", {}, AllowDuplicates | IntegerValues)
        .scan(uid.toInternalByteArray(), [&queriedRevisions](const QByteArray &, const QByteArray &value) {
            queriedRevisions << byteArrayToSizeT(value);
            return true;
        });

    return queriedRevisions;
}

QByteArray DataStore::getTypeFromRevision(const DataStore::Transaction &transaction, size_t revision)
{
    QByteArray type;
    transaction.openDatabase("revisionType", /* errorHandler = */ {}, IntegerKeys)
        .scan(revision,
            [&](const size_t, const QByteArray &value) -> bool {
                type = QByteArray{value.constData(), value.size()};
                return false;
            },
            [revision](const Error &error) { SinkWarning() << "Couldn't find type for revision " << revision; });
    Q_ASSERT(!type.isEmpty());
    return type;
}

void DataStore::recordRevision(DataStore::Transaction &transaction, size_t revision,
    const Identifier &uid, const QByteArray &type)
{
    const auto uidBa = uid.toInternalByteArray();
    transaction
        .openDatabase("revisions", /* errorHandler = */ {}, IntegerKeys)
        .write(revision, uidBa);
    transaction.openDatabase("uidsToRevisions", /* errorHandler = */ {}, AllowDuplicates | IntegerValues)
        .write(uidBa, sizeTToByteArray(revision));
    transaction
        .openDatabase("revisionType", /* errorHandler = */ {}, IntegerKeys)
        .write(revision, type);
}

void DataStore::removeRevision(DataStore::Transaction &transaction, size_t revision)
{
    const auto uid = getUidFromRevision(transaction, revision);

    transaction
        .openDatabase("revisions", /* errorHandler = */ {}, IntegerKeys)
        .remove(revision);
    transaction.openDatabase("uidsToRevisions", /* errorHandler = */ {}, AllowDuplicates | IntegerValues)
        .remove(uid.toInternalByteArray(), sizeTToByteArray(revision));
    transaction
        .openDatabase("revisionType", /* errorHandler = */ {}, IntegerKeys)
        .remove(revision);
}

void DataStore::recordUid(DataStore::Transaction &transaction, const Identifier &uid, const QByteArray &type)
{
    transaction.openDatabase(type + "uids", {}, IntegerKeys).write(uid.toInternalByteArray(), "");
}

void DataStore::removeUid(DataStore::Transaction &transaction, const Identifier &uid, const QByteArray &type)
{
    transaction.openDatabase(type + "uids", {}, IntegerKeys).remove(uid.toInternalByteArray());
}

void DataStore::getUids(const QByteArray &type, const Transaction &transaction, const std::function<void(const Identifier &uid)> &callback)
{
    transaction.openDatabase(type + "uids", {}, IntegerKeys).scan("", [&] (const QByteArray &key, const QByteArray &) {
        callback(Identifier::fromInternalByteArray(key));
        return true;
    });
}

bool DataStore::hasUid(const QByteArray &type, const Transaction &transaction, const Identifier &uid)
{
    bool hasTheUid = false;
    transaction.openDatabase(type + "uids", {}, IntegerKeys).scan(uid.toInternalByteArray(), [&](const QByteArray &key, const QByteArray &) {
        Q_ASSERT(uid.toInternalByteArray() == key);
        hasTheUid = true;
        return false;
    });

    return hasTheUid;
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
    return t.openDatabase(type + ".main", /* errorHandler= */ {}, IntegerKeys);
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
    transaction.openDatabase("__metadata").write("databaseVersion", QByteArray::number(revision));
}

qint64 DataStore::databaseVersion(const DataStore::Transaction &transaction)
{
    qint64 r = 0;
    transaction.openDatabase("__metadata").scan("databaseVersion",
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
