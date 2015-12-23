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

class Module
{
public:
    struct Syntax
    {
        Syntax();
        Syntax(const QString &keyword, std::function<bool(const QStringList &, State &)> lambda = std::function<bool(const QStringList &, State &)>(), const QString &helpText = QString(), bool eventDriven = false);
        QString keyword;
        std::function<bool(const QStringList &, State &)> lambda;
        QList<Syntax> children;
        QString help;
        bool eventDriven;
    };

    typedef std::pair<const Syntax *, QStringList> Command;

    static void addModule(const Module &module);
    static QList<Module> modules();
    static Command match(const QStringList &commands);
    static bool run(const QStringList &commands);
    static void loadModules();
    static QVector<Syntax>nearestSyntax(const QStringList &words, const QString &fragment);

    Module();
    Module::Syntax syntax() const;
    void setSyntax(const Syntax &syntax);

private:
    Command matches(const QStringList &commands) const;

    Syntax m_syntax;
    static QList<Module> s_modules;
    static State s_state;
};

