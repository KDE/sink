/*
 * Copyright (C) 2015 Christian Mollekopf <mollekopf@kolabsys.com>
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

#include "formatter.h"

#include <QDir>
#include <QObject>

#include <iostream>

namespace HAWD
{

void Formatter::print(const QString &datasetName, const QStringList &cols, State &state)
{
    QDir project(state.projectPath());
    Dataset dataset(datasetName, state);

    if (!dataset.isValid()) {
        std::cout << QObject::tr("The dataset %1 could not be loaded; try checking it with the check command").arg(datasetName).toStdString() << std::endl;
        return;
    }
    print(dataset, cols);
}

void Formatter::print(Dataset &dataset, const QStringList &cols)
{
    std::cout << dataset.tableHeaders(cols).toStdString() << std::endl;
    //Just reading doesn't sort the rows, let's use a map
    QMap<qint64, QString> rows;
    dataset.eachRow(
        [cols, &rows](const Dataset::Row &row) {
            rows.insert(row.key(), row.toString());
        });
    for (const auto &s : rows.values().mid(rows.size() - 10)) {
        std::cout << s.toStdString() << std::endl;
    }
}

}
