/*
 * Copyright (C) 2017 Christian Mollekopf <mollekopf@kolabsys.com>
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

#include "secretstore.h"

#include <QGlobalStatic>
#include <QMap>
#include <QString>
#include <QMutexLocker>

using namespace Sink;

QMutex SecretStore::sMutex;

SecretStore::SecretStore()
    : QObject()
{

}

SecretStore &SecretStore::instance()
{
    static SecretStore s;
    return s;
}

void SecretStore::insert(const QByteArray &resourceId, const QString &secret)
{
    QMutexLocker locker{&sMutex};
    mCache.insert(resourceId, secret);
    locker.unlock();
    emit secretAvailable(resourceId);
}

QString SecretStore::resourceSecret(const QByteArray &resourceId)
{
    QMutexLocker locker{&sMutex};
    return mCache.value(resourceId);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "moc_secretstore.cpp"
#pragma clang diagnostic pop
