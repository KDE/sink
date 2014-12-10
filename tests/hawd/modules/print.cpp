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

#include "print.h"

#include "../datasetdefinition.h"
#include "../dataset.h"

#include <QDir>
#include <QObject>

#include <iostream>

namespace HAWD
{

Print::Print()
    : Module()
{
    Syntax top("print", &Print::print);
    setSyntax(top);
    setDescription(QObject::tr("Prints a table from a dataset; you can provide a list of rows to output"));
}

bool Print::print(const QStringList &commands, State &state)
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

    QStringList cols;
    if (commands.size() > 1) {
        cols = commands;
        cols.removeFirst();
    }

    std::cout << dataset.tableHeaders(cols).toStdString() << std::endl;

    dataset.eachRow(
        [cols](const Dataset::Row &row) {
            std::cout << row.toString(cols).toStdString() << std::endl;
        });
    return true;
}

} // namespace HAWD

