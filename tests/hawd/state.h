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

#pragma once

#include <QStringList>
#include <QJsonObject>

#include "datasetdefinition.h"
#include "hawd_export.h"

namespace HAWD
{

class HAWD_EXPORT State
{
public:
    State(const QString &configPath = QString());

    bool isValid() const;
    QVariant configValue(const QString &key) const;
    QString resultsPath() const;
    QString projectPath() const;
    DatasetDefinition datasetDefinition(const QString &name) const;
    const char *commitHash() const;

private:
    void findGitHash();

    bool m_valid;
    QJsonObject m_configData;
    char m_commitHash[10];
};

} // namespace HAWD

