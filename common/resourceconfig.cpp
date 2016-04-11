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
#include <log.h>

static QSharedPointer<QSettings> getConfig(const QByteArray &identifier)
{
    return QSharedPointer<QSettings>::create(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/sink/" + identifier + ".ini", QSettings::IniFormat);
}

QByteArray ResourceConfig::newIdentifier(const QByteArray &type)
{
    auto settings = getConfig("resources");
    const auto counter = settings->value("instanceCounter", 0).toInt() + 1;
    const QByteArray identifier = type + ".instance" + QByteArray::number(counter);
    settings->setValue("instanceCounter", counter);
    settings->sync();
    return identifier;
}

void ResourceConfig::addResource(const QByteArray &identifier, const QByteArray &type)
{
    auto settings = getConfig("resources");
    settings->beginGroup(QString::fromLatin1(identifier));
    settings->setValue("type", type);
    settings->endGroup();
    settings->sync();
}

void ResourceConfig::removeResource(const QByteArray &identifier)
{
    auto settings = getConfig("resources");
    settings->beginGroup(QString::fromLatin1(identifier));
    settings->remove("");
    settings->endGroup();
    settings->sync();
    QFile::remove(getConfig(identifier)->fileName());
}

QMap<QByteArray, QByteArray> ResourceConfig::getResources()
{
    QMap<QByteArray, QByteArray> resources;
    auto settings = getConfig("resources");
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
    auto settings = getConfig("resources");
    settings->clear();
    settings->sync();
}

void ResourceConfig::configureResource(const QByteArray &identifier, const QMap<QByteArray, QVariant> &configuration)
{
    auto config = getConfig(identifier);
    config->clear();
    for (const auto &key : configuration.keys()) {
        config->setValue(key, configuration.value(key));
    }
    config->sync();
}

QMap<QByteArray, QVariant> ResourceConfig::getConfiguration(const QByteArray &identifier)
{
    QMap<QByteArray, QVariant> configuration;
    auto config = getConfig(identifier);
    for (const auto &key : config->allKeys()) {
        configuration.insert(key.toLatin1(), config->value(key));
    }
    return configuration;
}
