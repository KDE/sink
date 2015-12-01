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

#include "datasetdefinition.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <iostream>

namespace HAWD
{

static QHash<QString, QMetaType::Type> s_types;

DataDefinition::DataDefinition(const QString &name, QMetaType::Type type, const QString &unit, int min, int max)
    : m_name(name),
      m_type(type),
      m_unit(unit),
      m_min(min),
      m_max(max)
{
}

DataDefinition::DataDefinition(const QJsonObject &json)
    : m_name(json.value("name").toString()),
      m_type(QMetaType::Int),
      m_unit(json.value("unit").toString()),
      m_min(json.value("min").toInt()),
      m_max(json.value("max").toInt())
{
    if (s_types.isEmpty()) {
        s_types.insert("date", QMetaType::QDate);
        s_types.insert("time", QMetaType::QTime);
        s_types.insert("int", QMetaType::Int);
        s_types.insert("uint", QMetaType::UInt);
        s_types.insert("bool", QMetaType::Bool);
        s_types.insert("float", QMetaType::Float);
        s_types.insert("double", QMetaType::Double);
        s_types.insert("char", QMetaType::QChar);
        s_types.insert("string", QMetaType::QString);
        s_types.insert("datetime", QMetaType::QDateTime);
    }

    const QString typeString = json.value("type").toString().toLower();
    if (s_types.contains(typeString)) {
        m_type = s_types.value(typeString);
    }
}

QString DataDefinition::name() const
{
    return m_name;
}

QString DataDefinition::typeString() const
{
    return QMetaType::typeName(m_type);
}

QMetaType::Type DataDefinition::type() const
{
    return m_type;
}

QString DataDefinition::unit() const
{
    return m_unit;
}

int DataDefinition::min() const
{
    return m_min;
}

int DataDefinition::max() const
{
    return m_max;
}

DatasetDefinition::DatasetDefinition(const QString &path)
    : m_valid(false)
{
    QFile file(path);
    m_name = file.fileName();

    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(file.readAll(), &error);

        if (jsonDoc.isNull()) {
            m_lastError = QObject::tr("Dataset definition file malformed at character %1: %2").arg(error.offset).arg(error.errorString());
        } else {
            m_valid = true;
            QJsonObject json = jsonDoc.object();
            const QString name = json.value("name").toString();
            if (!name.isEmpty()) {
                m_name = name;
            }

            m_description = json.value("description").toString();
            auto cols = json.value("columns").toArray();
            for (const auto &entry: cols) {
                QJsonObject def = entry.toObject();
                if (!def.isEmpty()) {
                    m_columns << qMakePair(def.value("name").toString(), DataDefinition(def));
                }
            }
        }
    } else {
        m_lastError = QObject::tr("Could not open file for parsing: ").arg(path);
    }
}

bool DatasetDefinition::isValid() const
{
    return m_valid;
}

QString DatasetDefinition::lastError() const
{
    return m_lastError;
}

QString DatasetDefinition::name() const
{
    return m_name;
}

QString DatasetDefinition::description() const
{
    return m_description;
}

QList<QPair<QString, DataDefinition> > DatasetDefinition::columns() const
{
    return m_columns;
}

} // namespace HAWD

