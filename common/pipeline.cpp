/*
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

#include "pipeline.h"

#include <QStandardPaths>

namespace Akonadi2
{

class Pipeline::Private
{
public:
    Private(const QString &storageName)
        : storage(QStandardPaths::writableLocation(QStandardPaths::QStandardPaths::GenericDataLocation) + "/akonadi2", storageName, Akonadi2::Storage::ReadWrite)
    {

    }

    Akonadi2::Storage storage;
};

Pipeline::Pipeline(const QString &storageName)
    : d(new Private(storageName))
{
}

Pipeline::~Pipeline()
{
}

Storage &Pipeline::storage()
{
    return d->storage;
}

void Pipeline::null(uint messageId, const char *key, size_t keySize, const char *buffer, size_t bufferSize)
{
    d->storage.write(key, keySize, buffer, bufferSize);
}

void Pipeline::newEntity(uint messageId, const char *key, size_t keySize, const char *buffer, size_t bufferSize)
{
    d->storage.write(key, keySize, buffer, bufferSize);
}

void Pipeline::modifiedEntity(uint messageId, const char *key, size_t keySize, const char *buffer, size_t bufferSize)
{
    d->storage.write(key, keySize, buffer, bufferSize);
}

void Pipeline::deletedEntity(uint messageId, const char *key, size_t keySize, const char *buffer, size_t bufferSize)
{
    d->storage.write(key, keySize, buffer, bufferSize);
}

} // namespace Akonadi2

