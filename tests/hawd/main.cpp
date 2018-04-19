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

#include "module.h"

#include <QCoreApplication>
#include <QDebug>

#include <iostream>

void printHelp()
{
    std::cout << QCoreApplication::translate("main", "Usage of the How Are We Doing (hawd) command line tool:").toStdString() << std::endl;
    HAWD::Module::printCommands();
}

int main(int argc, char *argv[])
{
    // load all modules
    HAWD::Module::loadModules();
    HAWD::State state;

    if (!state.isValid()) {
        exit(1);
    }

    QCoreApplication app(argc, argv);
    app.setApplicationName("hawd");

    QStringList commands = app.arguments();
    commands.removeFirst();

    if (commands.isEmpty()) {
        printHelp();
    }

    return HAWD::Module::match(commands, state);
}
