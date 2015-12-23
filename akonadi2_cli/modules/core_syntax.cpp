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

#include "core_syntax.h"

#include <QObject> // tr()
#include <QSet>
#include <QTextStream>

namespace CoreSyntax
{

Module::SyntaxList syntax()
{
    Module::SyntaxList syntax;
    syntax << Module::Syntax("exit", QObject::tr("Exits the application. Ctrl-d also works!"), &CoreSyntax::exit);
    syntax << Module::Syntax(QObject::tr("help"), QObject::tr("Print command information: help [command]"), &CoreSyntax::showHelp);
    return syntax;
}

bool exit(const QStringList &, State &)
{
    ::exit(0);
    return true;
}

bool showHelp(const QStringList &commands, State &state)
{
    Module::Command command = Module::self()->match(commands);
    if (commands.isEmpty()) {
        state.printLine(QObject::tr("Welcome to the Akonadi2 command line tool!"));
        state.printLine(QObject::tr("Top-level commands:"));

        QSet<QString> sorted;
        for (auto syntax: Module::self()->syntax()) {
            sorted.insert(syntax.keyword);
        }

        for (auto keyword: sorted) {
            state.printLine(keyword, 1);
        }
    } else if (const Module::Syntax *syntax = command.first) {
        //TODO: get parent!
        state.print(QObject::tr("Command `%1`").arg(syntax->keyword));

        if (!syntax->help.isEmpty()) {
            state.print(": " + syntax->help);
        }
        state.printLine();

        if (!syntax->children.isEmpty()) {
            state.printLine("Sub-commands:", 1);
            QSet<QString> sorted;
            for (auto childSyntax: syntax->children) {
                sorted.insert(childSyntax.keyword);
            }

            for (auto keyword: sorted) {
                state.printLine(keyword, 1);
            }
        }
    } else {
        state.printError("Unknown command: " + commands.join(" "));
    }

    return true;
}

} // namespace CoreSyntax

