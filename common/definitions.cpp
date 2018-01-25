/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "definitions.h"

#include <QStandardPaths>
#include <QDir>

static bool rereadDataLocation = true;
static bool rereadConfigLocation = true;
static bool rereadTemporaryFileLocation = true;

void Sink::clearLocationCache()
{
    rereadDataLocation = true;
    rereadConfigLocation = true;
    rereadTemporaryFileLocation = true;
}

QString Sink::storageLocation()
{
    return dataLocation() + "/storage";
}

static QString sinkLocation(QStandardPaths::StandardLocation location)
{
    return QStandardPaths::writableLocation(location) + "/sink";
}

QString Sink::dataLocation()
{
    static QString location = sinkLocation(QStandardPaths::GenericDataLocation);
    //Warning: This is not threadsafe, but clearLocationCache is only ever used in testcode. The initialization above is required to make at least the initialization threadsafe (relies on C++11 threadsafe initialization).
    if (rereadDataLocation) {
        location = sinkLocation(QStandardPaths::GenericDataLocation);
        rereadDataLocation = false;
    }
    return location;
}

QString Sink::configLocation()
{
    static QString location = sinkLocation(QStandardPaths::GenericConfigLocation);
    //Warning: This is not threadsafe, but clearLocationCache is only ever used in testcode. The initialization above is required to make at least the initialization threadsafe (relies on C++11 threadsafe initialization).
    if (rereadConfigLocation) {
        location = sinkLocation(QStandardPaths::GenericConfigLocation);
        rereadConfigLocation = false;
    }
    return location;
}

QString Sink::temporaryFileLocation()
{
    static QString location = dataLocation() + "/temporaryFiles";
    static bool dirCreated = false;
    //Warning: This is not threadsafe, but clearLocationCache is only ever used in testcode. The initialization above is required to make at least the initialization threadsafe (relies on C++11 threadsafe initialization).
    if (rereadTemporaryFileLocation) {
        location = dataLocation() + "/temporaryFiles";
        dirCreated = QDir{}.mkpath(location);
        rereadTemporaryFileLocation = false;
    }
    if (!dirCreated && QDir{}.mkpath(location)) {
        dirCreated = true;
    }
    return location;
}

QString Sink::resourceStorageLocation(const QByteArray &resourceInstanceIdentifier)
{
    return storageLocation() + "/" + resourceInstanceIdentifier + "/data";
}


qint64 Sink::latestDatabaseVersion()
{
    return 1;
}
