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

#include "modules/list.h"

#include <QCoreApplication>

#include <iostream>

namespace HAWD
{

QVector<Module> Module::s_modules;

Module::Syntax::Syntax()
{
}

Module::Syntax::Syntax(const QString &k, std::function<bool(const QStringList &, State &)> l, bool e)
    : keyword(k),
      lambda(l),
      eventDriven(e)
{
}

Module::Module()
{
}

void Module::loadModules()
{
    addModule(List());
}

void Module::printCommands()
{
    for (const Module &module: s_modules) {
        printSyntax(1, module.syntax(), module.description());
    }
}

void Module::printSyntax(uint indent, const Syntax &syntax, const QString &description)
{
    const std::string indentation(indent, '\t');
    std::cout << indentation;

    if (indent < 2) {
        std::cout << "hawd ";
    }

    std::cout << syntax.keyword.toStdString();

    if (!description.isEmpty()) {
        std::cout << ": " << description.toStdString();
    }

    std::cout << std::endl;

    for (const Syntax &child: syntax.children) {
        printSyntax(indent + 1, child);
    }
}

void Module::addModule(const Module &module)
{
    s_modules.append(module);
}

QVector<Module> Module::modules()
{
    return s_modules;
}

bool Module::match(const QStringList &commands, State &state)
{
    for (const Module &module: s_modules) {
        if (module.matches(commands, state)) {
            return true;
        }
    }

    return false;
}

Module::Syntax Module::syntax() const
{
    return m_syntax;
}

void Module::setSyntax(const Syntax &syntax)
{
    m_syntax = syntax;
}

QString Module::description() const
{
    return m_description;
}

void Module::setDescription(const QString &description)
{
    m_description = description;
}

bool Module::matches(const QStringList &commands, State &state) const
{
    if (commands.isEmpty()) {
        return false;
    }

    QStringListIterator commandIt(commands);

    if (commandIt.next() != m_syntax.keyword) {
        return false;
    }

    QListIterator<Syntax> syntaxIt(m_syntax.children);
    const Syntax *syntax = &m_syntax;
    QStringList tailCommands;
    while (commandIt.hasNext() && syntaxIt.hasNext()) {
        const QString word = commandIt.next();
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
        while (commandIt.hasNext()) {
            tailCommands << commandIt.next();
        }

        bool rv = syntax->lambda(tailCommands, state);
        if (rv && syntax->eventDriven) {
            return QCoreApplication::instance()->exec();
        }

        return rv;
    }

    return false;
}

} // namespace HAWD
