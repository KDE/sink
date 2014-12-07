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

#pragma once

#include "state.h"

#include <QStringList>
#include <QVector>

namespace HAWD
{

class Module
{
public:
    struct Syntax
    {
        Syntax();
        Syntax(const QString &keyword, std::function<bool(const QStringList &, State &)> lambda = std::function<bool(const QStringList &, State &)>(), bool eventDriven = false);
        QString keyword;
        std::function<bool(const QStringList &, State &)> lambda;
        QList<Syntax> children;
        bool eventDriven;
    };

    static void addModule(const Module &module);
    static QVector<Module> modules();
    static bool match(const QStringList &commands, State &state);
    static void loadModules();
    static void printCommands();

    Module();

    Module::Syntax syntax() const;
    void setSyntax(const Syntax &syntax);

    QString description() const;
    void setDescription(const QString &description);

    bool matches(const QStringList &commands, State &state) const;

private:
    static void printSyntax(uint indent, const Syntax &syntax, const QString &description = QString());
    Syntax m_syntax;
    QString m_description;
    static QVector<Module> s_modules;
};

} // namespace HAWD
