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

#include "test.h"

#include <QStandardPaths>
#include <QDir>
#include <QDebug>

void Sink::Test::initTest()
{
    QStandardPaths::setTestModeEnabled(true);
    // qDebug() << "Removing " << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).removeRecursively();
    // qDebug() << "Removing " << QStandardPaths::writableLocation(QStandardPaths::DataLocation);
    QDir(QStandardPaths::writableLocation(QStandardPaths::DataLocation)).removeRecursively();
    // qDebug() << "Removing " << QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)).removeRecursively();
    // qDebug() << "Removing " << QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).removeRecursively();
    // qDebug() << "Removing " << QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively();
    // qDebug() << "Removing " << QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    QDir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)).removeRecursively();
}
