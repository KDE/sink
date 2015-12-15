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
#include <QFile>

static QSharedPointer<QSettings> getSettings()
{
    return QSharedPointer<QSettings>::create(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/resources.ini", QSettings::IniFormat);
}

static QSharedPointer<QSettings> getResourceConfig(const QByteArray &identifier)
{
    return QSharedPointer<QSettings>::create(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/akonadi2/" + identifier, QSettings::IniFormat);
}


void ResourceConfig::addResource(const QByteArray &identifier, const QByteArray &type)
{
    auto settings = getSettings();
    settings->beginGroup(QString::fromLatin1(identifier));
    settings->setValue("type", type);
    settings->setValue("enabled", true);
    settings->endGroup();
    settings->sync();
}

void ResourceConfig::removeResource(const QByteArray &identifier)
{
    auto settings = getSettings();
    settings->beginGroup(QString::fromLatin1(identifier));
    settings->remove("");
    settings->endGroup();
    settings->sync();
    QFile::remove(getResourceConfig(identifier)->fileName());
}

QMap<QByteArray, QByteArray> ResourceConfig::getResources()
{
    QMap<QByteArray, QByteArray> resources;
    auto settings = getSettings();
    for (const auto &identifier : settings->childGroups()) {
        settings->beginGroup(identifier);
        const auto type = settings->value("type").toByteArray();
        resources.insert(identifier.toLatin1(), type);
        settings->endGroup();
    }
    return resources;
}

void ResourceConfig::clear()
{
    auto settings = getSettings();
    settings->clear();
    settings->sync();
}

void ResourceConfig::configureResource(const QByteArray &identifier, const QMap<QByteArray, QVariant> &configuration)
{
    auto config = getResourceConfig(identifier);
    config->clear();
    for (const auto &key : configuration.keys()) {
        config->setValue(key, configuration.value(key));
    }
    config->sync();
}

QMap<QByteArray, QVariant> ResourceConfig::getConfiguration(const QByteArray &identifier)
{
    QMap<QByteArray, QVariant> configuration;
    auto config = getResourceConfig(identifier);
    for (const auto &key : config->allKeys()) {
        configuration.insert(key.toLatin1(), config->value(key));
    }
    return configuration;
}

