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
#include <QTime>
#include <QVector>

#include <functional>

class Syntax
{
public:
    typedef QVector<Syntax> List;

    enum Interactivity
    {
        NotInteractive = 0,
        EventDriven
    };

    Syntax();
    Syntax(const QString &keyword, const QString &helpText = QString(),
        std::function<bool(const QStringList &, State &)> lambda = std::function<bool(const QStringList &, State &)>(), Interactivity interactivity = NotInteractive);

    struct Argument {
        QString name;
        QString help;
        bool required = true;
        bool variadic = false;
    };

    struct ParameterOptions {
        QString name;
        QString help;
        bool required = false;
    };

    // TODO: add examples?
    QString keyword;
    QString help;
    QVector<Argument> arguments;
    QMap<QString, ParameterOptions> parameters;
    QMap<QString, QString> flags;
    Interactivity interactivity;

    void addPositionalArgument(const Argument &);
    void addParameter(const QString &name, const ParameterOptions &options);
    void addFlag(const QString &name, const QString &help);

    QString usage() const;

    /**
     * This function will be called to execute the command.
     *
     * @arguments: The command arguments
     * @state: The state object
     * @return: Return true for success and false for error. If the command is event driven, returning false will not start an event loop and abort immediately.
     * If the command is not event driven, returning false will set the exit code to 1.
     */
    std::function<bool(const QStringList &arguments, State &state)> lambda;
    std::function<QStringList(const QStringList &, const QString &, State &state)> completer;

    QVector<Syntax> children;
};

class SyntaxTree
{
public:
    typedef std::pair<const Syntax *, QStringList> Command;

    static SyntaxTree *self();

    int registerSyntax(std::function<Syntax::List()> f);
    Syntax::List syntax() const;
    Command match(const QStringList &commands) const;
    Syntax::List nearestSyntax(const QStringList &words, const QString &fragment) const;
    State &state();
    int run(const QStringList &commands);

    static QStringList tokenize(const QString &text);

    struct Options {
        QStringList positionalArguments;
        QMap<QString, QStringList> options;
    };
    static Options parseOptions(const QStringList &text);

private:
    SyntaxTree();

    Syntax::List m_syntax;
    State m_state;
    QTime m_timeElapsed;
    static SyntaxTree *s_module;
};

#define REGISTER_SYNTAX(name) static const int theTrickFor##name = SyntaxTree::self()->registerSyntax(&name::syntax);
