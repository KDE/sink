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

#include <QDebug>
#include <QObject> // tr()
#include <QSet>
#include <QTextStream>

namespace CoreSyntax
{

Syntax::List syntax()
{
    Syntax::List syntax;
    syntax << Syntax("exit", QObject::tr("Exits the application. Ctrl-d also works!"), &CoreSyntax::exit);

    Syntax help(QObject::tr("help"), QObject::tr("Print command information: help [command]"), &CoreSyntax::showHelp);
    help.completer = &CoreSyntax::showHelpCompleter;
    syntax << help;

    Syntax set(QObject::tr("set"), QObject::tr("Sets settings for the session"));
    set.children << Syntax(QObject::tr("debug"), QObject::tr("Set the debug level from 0 to 6"), &CoreSyntax::setDebugLevel);
    syntax << set;

    Syntax get(QObject::tr("get"), QObject::tr("Gets settings for the session"));
    get.children << Syntax(QObject::tr("debug"), QObject::tr("Set the debug level from 0 to 6"), &CoreSyntax::printDebugLevel);
    syntax << get;

    return syntax;
}

bool exit(const QStringList &, State &)
{
    ::exit(0);
    return true;
}

bool showHelp(const QStringList &commands, State &state)
{
    SyntaxTree::Command command = SyntaxTree::self()->match(commands);
    if (commands.isEmpty()) {
        state.printLine(QObject::tr("Welcome to the Akonadi2 command line tool!"));
        state.printLine(QObject::tr("Top-level commands:"));

        QSet<QString> sorted;
        for (auto syntax: SyntaxTree::self()->syntax()) {
            sorted.insert(syntax.keyword);
        }

        for (auto keyword: sorted) {
            state.printLine(keyword, 1);
        }
    } else if (const Syntax *syntax = command.first) {
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

QStringList showHelpCompleter(const QStringList &commands, const QString &fragment)
{
    QStringList items;

    for (auto syntax: SyntaxTree::self()->syntax()) {
        if (fragment.isEmpty() || syntax.keyword.startsWith(fragment)) {
            items << syntax.keyword;
        }
    }

    qSort(items);
    return items;
}

bool setDebugLevel(const QStringList &commands, State &state)
{
    if (commands.count() != 1) {
        state.printError(QObject::tr("Wrong number of arguments; expected 1 got %1").arg(commands.count()));
        return false;
    }

    bool ok = false;
    int level = commands[0].toUInt(&ok);

    if (!ok) {
        state.printError(QObject::tr("Expected a number between 0 and 6, got %1").arg(commands[0]));
        return false;
    }

    state.setDebugLevel(level);
    return true;
}

bool printDebugLevel(const QStringList &commands, State &state)
{
    state.printLine(QString::number(state.debugLevel()));
    return true;
}

} // namespace CoreSyntax

