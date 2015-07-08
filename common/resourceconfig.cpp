/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */
#include "resourceconfig.h"

#include <QSettings>
#include <QSharedPointer>
#include <QStandardPaths>

static QSharedPointer<QSettings> getSettings()
{
    return QSharedPointer<QSettings>::create(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/resources.ini", QSettings::IniFormat);
}

void ResourceConfig::addResource(const QByteArray &identifier, const QByteArray &type)
{
    auto settings = getSettings();
    settings->beginGroup("resources");
    settings->setValue(QString::fromLatin1(identifier), type);
    settings->endGroup();
    // settings->beginGroup(identifier);
    // //Add some settings?
    // settings->endGroup();
    settings->sync();
}

void ResourceConfig::removeResource(const QByteArray &identifier)
{
    auto settings = getSettings();
    settings->beginGroup("resources");
    settings->remove(QString::fromLatin1(identifier));
    settings->endGroup();
    settings->sync();
}

QList<QPair<QByteArray, QByteArray> > ResourceConfig::getResources()
{
    QList<QPair<QByteArray, QByteArray> > resources;
    auto settings = getSettings();
    settings->beginGroup("resources");
    for (const auto &identifier : settings->childKeys()) {
        const auto type = settings->value(identifier).toByteArray();
        resources << qMakePair<QByteArray, QByteArray>(identifier.toLatin1(), type);
    }
    settings->endGroup();
    return resources;
}
