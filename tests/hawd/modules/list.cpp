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

#include "list.h"

#include "../datasetdefinition.h"

#include <QDebug>
#include <QDir>
#include <QObject>

#include <iostream>

namespace HAWD
{

List::List()
    : Module()
{
    Syntax top("list", &List::list);
    //top.children << Syntax("create", &List::create);
    setSyntax(top);
}

bool List::list(const QStringList &commands, State &state)
{
    QDir project(state.projectPath());

    if (commands.isEmpty()) {
        project.setFilter(QDir::Files | QDir::Readable | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        const QStringList entryList = project.entryList();

        if (entryList.isEmpty()) {
            std::cout << QObject::tr("No data sets in this project").toStdString() << std::endl;
        } else {
            std::cout << QObject::tr("Data sets in this project:").toStdString() << std::endl;
            for (const QString &file: project.entryList()) {
                std::cout << '\t' << file.toStdString() << std::endl;
            }
        }
    } else {
        for (const QString &file: commands) {
            DatasetDefinition dataset(project.absoluteFilePath(file));
            if (dataset.isValid()) {
                DatasetDefinition dataset(project.absoluteFilePath(file));
                std::cout << '\t' << QObject::tr("Dataset: %1").arg(dataset.name()).toStdString() << std::endl;
                QHashIterator<QString, DataDefinition> it(dataset.columns());
                while (it.hasNext()) {
                    it.next();
                    std::cout << "\t\t" << it.value().typeString().toStdString() << ' ' << it.key().toStdString() << std::endl;
                }

            } else {
                std::cout << QObject::tr("Invalid or non-existent dataset definition at %1").arg(project.absoluteFilePath(file)).toStdString() << std::endl;
            }
        }
    }

    return true;
}

} // namespace HAWD

