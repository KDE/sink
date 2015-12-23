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

// TODO: needs a proper registry; making "core" modules plugins is
//       almost certainly overkill, but this is not the way either
#include "modules/exit/exit.h"
#include "modules/help/help.h"

QList<Module> Module::s_modules;
State Module::s_state;

Module::Syntax::Syntax()
{
}

Module::Syntax::Syntax(const QString &k, std::function<bool(const QStringList &, State &)> l, const QString &helpText, bool e)
    : keyword(k),
      lambda(l),
      help(helpText),
      eventDriven(e)
{
}

Module::Module()
{
}

void Module::loadModules()
{
    addModule(CLI::Exit());
    addModule(CLI::Help());
}

void Module::addModule(const Module &module)
{
    s_modules.append(module);
}

QList<Module> Module::modules()
{
    return s_modules;
}

Module::Command Module::match(const QStringList &commands)
{
    Command command;
    for (const Module &module: s_modules) {
        command = module.matches(commands);
        if (command.first) {
            break;
        }
    }

    return command;
}

Module::Syntax Module::syntax() const
{
    return m_syntax;
}

void Module::setSyntax(const Syntax &syntax)
{
    m_syntax = syntax;
}

bool Module::run(const QStringList &commands)
{
    Command command = match(commands);
    if (command.first && command.first->lambda) {
        bool rv = command.first->lambda(command.second, s_state);
        if (rv && command.first->eventDriven) {
            return QCoreApplication::instance()->exec();
        }

        return rv;
    }

    return false;
}

Module::Command Module::matches(const QStringList &commandLine) const
{
    if (commandLine.isEmpty()) {
        return Command();
    }

    QStringListIterator commandLineIt(commandLine);

    if (commandLineIt.next() != m_syntax.keyword) {
        return Command();
    }

    QListIterator<Syntax> syntaxIt(m_syntax.children);
    const Syntax *syntax = &m_syntax;
    QStringList tailCommands;
    while (commandLineIt.hasNext() && syntaxIt.hasNext()) {
        const QString word = commandLineIt.next();
        while (syntaxIt.hasNext()) {
            const Syntax &child = syntaxIt.next();
            if (word == child.keyword) {
                syntax = &child;
                syntaxIt = child.children;
            }
        }

        if (!syntaxIt.hasNext()) {
            tailCommands << word;
            break;
        }
    }

    if (syntax && syntax->lambda) {
        while (commandLineIt.hasNext()) {
            tailCommands << commandLineIt.next();
        }

        return std::make_pair(syntax, tailCommands);
    }

    return Command();
}

QVector<Module::Syntax> Module::nearestSyntax(const QStringList &words, const QString &fragment)
{
    QVector<Module::Syntax> matches;

    //qDebug() << "words are" << words;
    if (words.isEmpty()) {
        for (const Module &module: s_modules) {
            if (module.syntax().keyword.startsWith(fragment)) {
                matches.push_back(module.syntax());
            }
        }
    } else {
        QStringListIterator wordIt(words);
        QString word = wordIt.next();
        Syntax lastFullSyntax;

        for (const Module &module: s_modules) {
            if (module.syntax().keyword == word) {
                lastFullSyntax = module.syntax();
                QListIterator<Syntax> syntaxIt(module.syntax().children);
                while (wordIt.hasNext()) {
                    word = wordIt.next();
                    while (syntaxIt.hasNext()) {
                        const Syntax &child = syntaxIt.next();
                        if (word == child.keyword) {
                            lastFullSyntax = child;
                            syntaxIt = child.children;
                        }
                    }
                }

                break;
            }
        }

        //qDebug() << "exiting with" << lastFullSyntax.keyword << words.last();
        if (lastFullSyntax.keyword == words.last()) {
            QListIterator<Syntax> syntaxIt(lastFullSyntax.children);
            while (syntaxIt.hasNext()) {
                Syntax syntax = syntaxIt.next();
                if (fragment.isEmpty() || syntax.keyword.startsWith(fragment)) {
                    matches.push_back(syntax);
                }
            }
        }
    }

    return matches;
}


