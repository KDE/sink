/*
 *   Copyright (C) 2016 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include "configstore.h"

#include <QSettings>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QFile>
#include <log.h>

SINK_DEBUG_AREA("configstore")

static QSharedPointer<QSettings> getConfig(const QByteArray &identifier)
{
    return QSharedPointer<QSettings>::create(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/sink/" + identifier + ".ini", QSettings::IniFormat);
}

ConfigStore::ConfigStore(const QByteArray &identifier)
    : mIdentifier(identifier),
    mConfig(getConfig(identifier))
{

}

QMap<QByteArray, QByteArray> ConfigStore::getEntries()
{
    QMap<QByteArray, QByteArray> resources;
    for (const auto &identifier : mConfig->childGroups()) {
        mConfig->beginGroup(identifier);
        const auto type = mConfig->value("type").toByteArray();
        resources.insert(identifier.toLatin1(), type);
        mConfig->endGroup();
    }
    return resources;
}

void ConfigStore::add(const QByteArray &identifier, const QByteArray &type)
{
    SinkTrace() << "Adding " << identifier;
    mConfig->beginGroup(QString::fromLatin1(identifier));
    mConfig->setValue("type", type);
    mConfig->endGroup();
    mConfig->sync();
}

void ConfigStore::remove(const QByteArray &identifier)
{
    SinkTrace() << "Removing " << identifier;
    mConfig->beginGroup(QString::fromLatin1(identifier));
    mConfig->remove("");
    mConfig->endGroup();
    mConfig->sync();
    QFile::remove(getConfig(identifier)->fileName());
}

void ConfigStore::clear()
{
    mConfig->clear();
    mConfig->sync();
}

void ConfigStore::modify(const QByteArray &identifier, const QMap<QByteArray, QVariant> &configuration)
{
    SinkTrace() << "Modifying " << identifier;
    auto config = getConfig(identifier);
    config->clear();
    for (const auto &key : configuration.keys()) {
        config->setValue(key, configuration.value(key));
    }
    config->sync();
}

QMap<QByteArray, QVariant> ConfigStore::get(const QByteArray &identifier)
{
    QMap<QByteArray, QVariant> configuration;
    auto config = getConfig(identifier);
    for (const auto &key : config->allKeys()) {
        configuration.insert(key.toLatin1(), config->value(key));
    }
    return configuration;
}

