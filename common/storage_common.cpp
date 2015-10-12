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

#include <iostream>

namespace Akonadi2
{

static const char *s_internalPrefix = "__internal";
static const int s_internalPrefixSize = strlen(s_internalPrefix);

void errorHandler(const Storage::Error &error)
{
    //TODO: allow this to be turned on / off globally
    //TODO: log $SOMEWHERE $SOMEHOW rather than just spit to stderr
    std::cout << "Read error in " << error.store.toStdString() << ", code " << error.code << ", message: " << error.message.toStdString() << std::endl;
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

void Storage::setMaxRevision(Akonadi2::Storage::Transaction &transaction, qint64 revision)
{
    transaction.openDatabase().write("__internal_maxRevision", QByteArray::number(revision));
}

qint64 Storage::maxRevision(const Akonadi2::Storage::Transaction &transaction)
{
    qint64 r = 0;
    transaction.openDatabase().scan("__internal_maxRevision", [&](const QByteArray &, const QByteArray &revision) -> bool {
        r = revision.toLongLong();
        return false;
    }, [](const Error &error){
        if (error.code != Akonadi2::Storage::NotFound) {
            std::cout << "Coultn'd find the maximum revision" << std::endl;
        }
    });
    return r;
}

void Storage::setCleanedUpRevision(Akonadi2::Storage::Transaction &transaction, qint64 revision)
{
    transaction.openDatabase().write("__internal_cleanedUpRevision", QByteArray::number(revision));
}

qint64 Storage::cleanedUpRevision(const Akonadi2::Storage::Transaction &transaction)
{
    qint64 r = 0;
    transaction.openDatabase().scan("__internal_cleanedUpRevision", [&](const QByteArray &, const QByteArray &revision) -> bool {
        r = revision.toLongLong();
        return false;
    }, [](const Error &error){
        if (error.code != Akonadi2::Storage::NotFound) {
            std::cout << "Coultn'd find the maximum revision" << std::endl;
        }
    });
    return r;
}

QByteArray Storage::getUidFromRevision(const Akonadi2::Storage::Transaction &transaction, qint64 revision)
{
    QByteArray uid;
    transaction.openDatabase("revisions").scan(QByteArray::number(revision), [&](const QByteArray &, const QByteArray &value) -> bool {
        uid = value;
        return false;
    }, [revision](const Error &error){
        std::cout << "Coultn'd find uid for revision " << revision << std::endl;
    });
    return uid;
}

QByteArray Storage::getTypeFromRevision(const Akonadi2::Storage::Transaction &transaction, qint64 revision)
{
    QByteArray type;
    transaction.openDatabase("revisionType").scan(QByteArray::number(revision), [&](const QByteArray &, const QByteArray &value) -> bool {
        type = value;
        return false;
    }, [revision](const Error &error){
        std::cout << "Coultn'd find type for revision " << revision << std::endl;
    });
    return type;
}

void Storage::recordRevision(Akonadi2::Storage::Transaction &transaction, qint64 revision, const QByteArray &uid, const QByteArray &type)
{
    //TODO use integerkeys
    transaction.openDatabase("revisions").write(QByteArray::number(revision), uid);
    transaction.openDatabase("revisionType").write(QByteArray::number(revision), type);
}

void Storage::removeRevision(Akonadi2::Storage::Transaction &transaction, qint64 revision)
{
    transaction.openDatabase("revisions").remove(QByteArray::number(revision));
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
    Q_ASSERT(key.size() == 38);
    return key + QByteArray::number(revision);
}

QByteArray Storage::uidFromKey(const QByteArray &key)
{
    return key.mid(0, 38);
}

} // namespace Akonadi2
