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
#include <QUuid>

SINK_DEBUG_AREA("storage")

QDebug& operator<<(QDebug &dbg, const Sink::Storage::DataStore::Error &error)
{
    dbg << error.message;
    return dbg;
}

namespace Sink {
namespace Storage {

static const char *s_internalPrefix = "__internal";
static const int s_internalPrefixSize = strlen(s_internalPrefix);

void errorHandler(const DataStore::Error &error)
{
    SinkWarning() << "Database error in " << error.store << ", code " << error.code << ", message: " << error.message;
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
                SinkWarning() << "Coultn'd find the maximum revision.";
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
                SinkWarning() << "Coultn'd find the maximum revision.";
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
                uid = value;
                return false;
            },
            [revision](const Error &error) { SinkWarning() << "Coultn'd find uid for revision: " << revision << error.message; });
    return uid;
}

QByteArray DataStore::getTypeFromRevision(const DataStore::Transaction &transaction, qint64 revision)
{
    QByteArray type;
    transaction.openDatabase("revisionType")
        .scan(QByteArray::number(revision),
            [&](const QByteArray &, const QByteArray &value) -> bool {
                type = value;
                return false;
            },
            [revision](const Error &error) { SinkWarning() << "Coultn'd find type for revision " << revision; });
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
    Q_ASSERT(key.size() == 38);
    return key + QByteArray::number(revision).rightJustified(19, '0', false);
}

QByteArray DataStore::uidFromKey(const QByteArray &key)
{
    return key.mid(0, 38);
}

qint64 DataStore::revisionFromKey(const QByteArray &key)
{
    return key.mid(39).toLongLong();
}

QByteArray DataStore::generateUid()
{
    return QUuid::createUuid().toByteArray();
}

DataStore::NamedDatabase DataStore::mainDatabase(const DataStore::Transaction &t, const QByteArray &type)
{
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
        [this](const DataStore::Error &error) {}, true);
    return found;
}

}
} // namespace Sink
