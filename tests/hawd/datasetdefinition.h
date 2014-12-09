/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QVariant>

namespace HAWD
{

class DataDefinition
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

class DatasetDefinition
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

