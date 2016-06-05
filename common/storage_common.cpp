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

namespace Sink {

static const char *s_internalPrefix = "__internal";
static const int s_internalPrefixSize = strlen(s_internalPrefix);

void errorHandler(const Storage::Error &error)
{
    Warning() << "Database error in " << error.store << ", code " << error.code << ", message: " << error.message;
}

std::function<void(const Storage::Error &error)> Storage::basicErrorHandler()
{
    return errorHandler;
}

void Storage::setDefaultErrorHandler(const std::function<void(const Storage::Error &error)> &errorHandler)
{
    mErrorHandler = errorHandler;
}

std::function<void(const Storage::Error &error)> Storage::defaultErrorHandler() const
{
    if (mErrorHandler) {
        return mErrorHandler;
    }
    return basicErrorHandler();
}

void Storage::setMaxRevision(Sink::Storage::Transaction &transaction, qint64 revision)
{
    transaction.openDatabase().write("__internal_maxRevision", QByteArray::number(revision));
}

qint64 Storage::maxRevision(const Sink::Storage::Transaction &transaction)
{
    qint64 r = 0;
    transaction.openDatabase().scan("__internal_maxRevision",
        [&](const QByteArray &, const QByteArray &revision) -> bool {
            r = revision.toLongLong();
            return false;
        },
        [](const Error &error) {
            if (error.code != Sink::Storage::NotFound) {
                Warning() << "Coultn'd find the maximum revision.";
            }
        });
    return r;
}

void Storage::setCleanedUpRevision(Sink::Storage::Transaction &transaction, qint64 revision)
{
    transaction.openDatabase().write("__internal_cleanedUpRevision", QByteArray::number(revision));
}

qint64 Storage::cleanedUpRevision(const Sink::Storage::Transaction &transaction)
{
    qint64 r = 0;
    transaction.openDatabase().scan("__internal_cleanedUpRevision",
        [&](const QByteArray &, const QByteArray &revision) -> bool {
            r = revision.toLongLong();
            return false;
        },
        [](const Error &error) {
            if (error.code != Sink::Storage::NotFound) {
                Warning() << "Coultn'd find the maximum revision.";
            }
        });
    return r;
}

QByteArray Storage::getUidFromRevision(const Sink::Storage::Transaction &transaction, qint64 revision)
{
    QByteArray uid;
    transaction.openDatabase("revisions")
        .scan(QByteArray::number(revision),
            [&](const QByteArray &, const QByteArray &value) -> bool {
                uid = value;
                return false;
            },
            [revision](const Error &error) { Warning() << "Coultn'd find uid for revision: " << revision << error.message; });
    return uid;
}

QByteArray Storage::getTypeFromRevision(const Sink::Storage::Transaction &transaction, qint64 revision)
{
    QByteArray type;
    transaction.openDatabase("revisionType")
        .scan(QByteArray::number(revision),
            [&](const QByteArray &, const QByteArray &value) -> bool {
                type = value;
                return false;
            },
            [revision](const Error &error) { Warning() << "Coultn'd find type for revision " << revision; });
    return type;
}

void Storage::recordRevision(Sink::Storage::Transaction &transaction, qint64 revision, const QByteArray &uid, const QByteArray &type)
{
    // TODO use integerkeys
    transaction.openDatabase("revisions").write(QByteArray::number(revision), uid);
    transaction.openDatabase("revisionType").write(QByteArray::number(revision), type);
}

void Storage::removeRevision(Sink::Storage::Transaction &transaction, qint64 revision)
{
    transaction.openDatabase("revisions").remove(QByteArray::number(revision));
    transaction.openDatabase("revisionType").remove(QByteArray::number(revision));
}

bool Storage::isInternalKey(const char *key)
{
    return key && strncmp(key, s_internalPrefix, s_internalPrefixSize) == 0;
}

bool Storage::isInternalKey(void *key, int size)
{
    if (size < 1) {
        return false;
    }

    return key && strncmp(static_cast<char *>(key), s_internalPrefix, (size > s_internalPrefixSize ? s_internalPrefixSize : size)) == 0;
}

bool Storage::isInternalKey(const QByteArray &key)
{
    return key.startsWith(s_internalPrefix);
}

QByteArray Storage::assembleKey(const QByteArray &key, qint64 revision)
{
    Q_ASSERT(revision <= 9223372036854775807);
    Q_ASSERT(key.size() == 38);
    return key + QByteArray::number(revision).rightJustified(19, '0', false);
}

QByteArray Storage::uidFromKey(const QByteArray &key)
{
    return key.mid(0, 38);
}

qint64 Storage::revisionFromKey(const QByteArray &key)
{
    return key.mid(39).toLongLong();
}

QByteArray Storage::generateUid()
{
    return QUuid::createUuid().toByteArray();
}

Storage::NamedDatabase Storage::mainDatabase(const Sink::Storage::Transaction &t, const QByteArray &type)
{
    return t.openDatabase(type + ".main");
}

bool Storage::NamedDatabase::contains(const QByteArray &uid)
{
    bool found = false;
    scan(uid,
        [&found](const QByteArray &, const QByteArray &) -> bool {
            found = true;
            return false;
        },
        [this](const Sink::Storage::Error &error) {}, true);
    return found;
}

} // namespace Sink
