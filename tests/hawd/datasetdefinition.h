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

#include <QHash>
#include <QJsonObject>
#include <QVariant>

#include "hawd_export.h"

namespace HAWD
{

class HAWD_EXPORT DataDefinition
{
public:
    DataDefinition(const QString &name = QString(), QMetaType::Type type = QMetaType::Void, const QString &unit = QString(), int min = 0, int max = 0);
    DataDefinition(const QJsonObject &object);

    QString name() const;
    QMetaType::Type type() const;
    QString typeString() const;
    QString unit() const;
    int min() const;
    int max() const;

private:
    QString m_name;
    QMetaType::Type m_type;
    QString m_unit;
    int m_min;
    int m_max;
};

class HAWD_EXPORT DatasetDefinition
{
public:
    DatasetDefinition(const QString &path);

    bool isValid() const;

    QString lastError() const;
    QString name() const;
    QString description() const;
    QHash<QString, DataDefinition> columns() const;

private:
    bool m_valid;
    QString m_name;
    QString m_description;
    QString m_lastError;
    QHash<QString, DataDefinition> m_columns;
};

} // namespace HAWD

