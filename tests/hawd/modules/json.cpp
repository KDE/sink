/*
 *   Copyright (C) 2017 Christian Mollekopf <mollekopf@kolabsystems.com>
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

#include "json.h"

#include "../datasetdefinition.h"
#include "../dataset.h"

#include <QDir>
#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

#include <iostream>

namespace HAWD
{

Json::Json()
    : Module()
{
    Syntax top("json", &Json::toJson);
    setSyntax(top);
    setDescription(QObject::tr("Prints a table from a dataset to json; you can provide a list of rows to output"));
}

bool Json::toJson(const QStringList &commands, State &state)
{
    if (commands.isEmpty()) {
        std::cout << QObject::tr("print requires a dataset to be named").toStdString() << std::endl;
        return true;
    }

    const QString datasetName = commands.first();

    QDir project(state.projectPath());
    Dataset dataset(datasetName, state);

    if (!dataset.isValid()) {
        std::cout << QObject::tr("The dataset %1 could not be loaded; try checking it with the check command").arg(datasetName).toStdString() << std::endl;
        return true;
    }

    auto definition = state.datasetDefinition(datasetName);

    QJsonObject json;
    json.insert("dataset", datasetName);
    json.insert("description", definition.description());

    const auto columns = definition.columns();

    QJsonArray array;
    dataset.eachRow(
        [&](const Dataset::Row &row) {
            QJsonObject jsonRow;
            jsonRow.insert("timestamp", QJsonValue::fromVariant(row.timestamp()));
            jsonRow.insert("commit", row.commitHash());
            QJsonArray columnsArray;
            for (const auto &col : columns) {
                QJsonObject columnObject;
                columnObject.insert("unit", QJsonValue::fromVariant(col.second.unit()));
                columnObject.insert("name", QJsonValue::fromVariant(col.first));
                columnObject.insert("value", QJsonValue::fromVariant(row.value(col.first)));
                columnsArray << columnObject;
            }
            jsonRow.insert("columns", columnsArray);
            array.append(jsonRow);
        });
    json.insert("rows", array);
    std::cout << QJsonDocument{json}.toJson().toStdString() << std::endl;
    return true;
}

} // namespace HAWD

