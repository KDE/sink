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

#include "check.h"

#include "../datasetdefinition.h"

#include <QDir>
#include <QObject>

#include <iostream>

namespace HAWD
{

Check::Check()
    : Module()
{
    Syntax top("check", &Check::check);
    setDescription(QObject::tr("Checks a dataset description for validity and prints out any errors it finds"));
    setSyntax(top);
}

bool Check::check(const QStringList &commands, State &state)
{
    if (commands.isEmpty()) {
        std::cout << QObject::tr("Please provide the name of a dataset definition file. (Use the 'list' command to see available datasets.)").toStdString() << std::endl;
    } else {
        for (const QString &name: commands) {
            if (name == "*") {
                QDir project(state.projectPath());
                project.setFilter(QDir::Files | QDir::Readable | QDir::NoDotAndDotDot | QDir::NoSymLinks);
                for (const QString &entry: project.entryList()) {
                    checkFile(entry, state);
                }
            } else {
                checkFile(name, state);
            }
        }
    }

    return true;
}

void Check::checkFile(const QString &name, State &state)
{
    DatasetDefinition def = state.datasetDefinition(name);
    if (def.isValid()) {
        std::cout << QObject::tr("%1 is OK").arg(name).toStdString() << std::endl;
    } else {
        std::cout << QObject::tr("%1 has errors: %2").arg(name).arg(def.lastError()).toStdString() << std::endl;
    }
}

} // namespace HAWD

