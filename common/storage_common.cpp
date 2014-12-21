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

#include "storage.h"

#include <iostream>

namespace Akonadi2
{

void errorHandler(const Storage::Error &error)
{
    //TODO: allow this to be turned on / off globally
    //TODO: log $SOMEWHERE $SOMEHOW rather than just spit to stderr
    std::cerr << "Read error in " << error.store << ", code " << error.code << ", message: " << error.message << std::endl;
}

std::function<void(const Storage::Error &error)> Storage::basicErrorHandler()
{
    return errorHandler;
}

void Storage::read(const std::string &sKey, const std::function<bool(const std::string &value)> &resultHandler)
{
    read(sKey, resultHandler, &errorHandler);
}

void Storage::read(const std::string &sKey, const std::function<bool(void *ptr, int size)> &resultHandler)
{
    read(sKey, resultHandler, &errorHandler);
}

void Storage::scan(const std::string &sKey, const std::function<bool(void *keyPtr, int keySize, void *valuePtr, int valueSize)> &resultHandler)
{
    scan(sKey.data(), sKey.size(), resultHandler, &errorHandler);
}

void Storage::setMaxRevision(qint64 revision)
{
    write("__internal_maxRevision", QString::number(revision).toStdString());
}

qint64 Storage::maxRevision()
{
    qint64 r = 0;
    read(std::string("__internal_maxRevision"), [&](const std::string &revision) -> bool {
        r = QString::fromStdString(revision).toLongLong();
        return false;
    },
    [](const Storage::Error &error) {
        //Ignore the error in case we don't find the value
        //TODO only ignore value not found errors
    });
    return r;
}

} // namespace Akonadi2
