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

