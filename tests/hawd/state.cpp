/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
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

#include "state.h"

#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QObject>

#include <iostream>

static const QString configFileName("hawd.conf");

namespace HAWD
{

State::State()
    : m_valid(true)
{
    QDir dir;
    QString configPath;

    while (!dir.exists(configFileName) && dir.cdUp()) { }

    if (dir.exists(configFileName)) {
        configPath = dir.absoluteFilePath(configFileName);
    }

    if (configPath.isEmpty()) {
        std::cerr << QObject::tr("Could not find hawd configuration. A hawd.conf file must be in the current directory or in a directory above it.").toStdString() << std::endl;
        m_valid = false;
        return;
    }

    QFile configFile(configPath);
    if (configFile.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        QJsonDocument config = QJsonDocument::fromJson(configFile.readAll(), &error);
        if (config.isNull()) {
            std::cerr << QObject::tr("Error parsing config file at %1").arg(configPath).toStdString() << std::endl;
            std::cerr << '\t' << error.errorString().toStdString();
        } else {
            m_configData = config.object();
        }
    }
}

bool State::isValid() const
{
    return m_valid;
}

QString tildeExpand(QString path)
{
    if (path.isEmpty() || path.at(0) != '~') {
        return path;
    }

    return path.replace('~', QDir::homePath());
}

QString State::resultsPath() const
{
    return tildeExpand(configValue("results").toString());
}

QString State::projectPath() const
{
    return tildeExpand(configValue("project").toString());
}

DatasetDefinition State::datasetDefinition(const QString &name) const
{
    return DatasetDefinition(projectPath() + '/' + name);
}

QVariant State::configValue(const QString &key) const
{
    return m_configData.value(key).toVariant();
}

} // namespace HAWD
