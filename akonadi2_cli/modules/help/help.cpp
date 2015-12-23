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

#include "help.h"

#include <QObject>
#include <QSet>
#include <QTextStream>

#include "module.h"

namespace CLI
{

Help::Help()
{
    Syntax topLevel = Syntax(QObject::tr("help"), &Help::showHelp, QObject::tr("Print command information: help [command]"));
    setSyntax(topLevel);
}

bool Help::showHelp(const QStringList &commands, State &)
{
    Module::Command command = Module::match(commands);
    QTextStream stream(stdout);
    if (commands.isEmpty()) {
        stream << QObject::tr("Welcome to the Akonadi2 command line tool!") << "\n";
        stream << QObject::tr("Top-level commands:") << "\n";
        QSet<QString> sorted;
        for (auto module: Module::modules()) {
            sorted.insert(module.syntax().keyword);
        }

        for (auto keyword: sorted) {
            stream << "\t" << keyword << "\n";
        }
    } else if (const Syntax *syntax = command.first) {
        //TODO: get parent!
        stream << QObject::tr("Command `%1`").arg(syntax->keyword);

        if (!syntax->help.isEmpty()) {
            stream << ": " << syntax->help;
        }
        stream << "\n";

        if (!syntax->children.isEmpty()) {
            stream << "\tSub-commands:\n";
            QSet<QString> sorted;
            for (auto childSyntax: syntax->children) {
                sorted.insert(childSyntax.keyword);
            }

            for (auto keyword: sorted) {
                stream << "\t" << keyword << "\n";
            }
        }
    } else {
        stream << "Unknown command: " << commands.join(" ") << "\n";
    }

    return true;
}

} // namespace CLI

