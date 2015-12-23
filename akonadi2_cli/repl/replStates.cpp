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

#include "replStates.h"

#include <unistd.h>
#include <iostream>

#include <readline/readline.h>
#include <readline/history.h>

#include <QDebug>
#include <QEvent>
#include <QStateMachine>

#include "syntaxtree.h"

static char *akonadi2_cli_next_tab_complete_match(const char *text, int state);
static char ** akonadi2_cli_tab_completion(const char *text, int start, int end);

ReadState::ReadState(QState *parent)
    : QState(parent)
{
    rl_completion_entry_function = akonadi2_cli_next_tab_complete_match;
    rl_attempted_completion_function = akonadi2_cli_tab_completion;
}

void ReadState::onEntry(QEvent *event)
{
    Q_UNUSED(event)
    char *line = readline(prompt());

    if (!line) {
        std::cout << std::endl;
        emit exitRequested();
        return;
    }

    // we have actual data, so let's wait for a full line of text
    QByteArray input(line);
    const QString text = QString(line).simplified();
    //qDebug() << "Line is ... " << text;

    if (text.length() > 0) {
        add_history(line);
    }

    free(line);
    emit command(text);
}

const char *ReadState::prompt() const
{
    return "> ";
}

UnfinishedReadState::UnfinishedReadState(QState *parent)
    : ReadState(parent)
{
}

const char *UnfinishedReadState::prompt() const
{
    return "  ";
}

EvalState::EvalState(QState *parent)
    : QState(parent)
{
}

void EvalState::onEntry(QEvent *event)
{
    QStateMachine::SignalEvent *e = dynamic_cast<QStateMachine::SignalEvent*>(event);

    const QString command = e ? e->arguments()[0].toString() : QString();

    if (command.isEmpty()) {
        complete();
        return;
    }

    if (command.right(1) == "\\") {
        m_partial += " " + command.left(command.size() - 1);
        continueInput();
    } else {
        m_partial += " " + command;
        complete();
    }
}

void EvalState::complete()
{
    m_partial = m_partial.simplified();

    if (!m_partial.isEmpty()) {
        //emit output("Processing ... " + command);
        const QStringList commands = SyntaxTree::tokenize(m_partial);
        SyntaxTree::self()->run(commands);
        m_partial.clear();
    }

    emit completed();
}

PrintState::PrintState(QState *parent)
    : QState(parent)
{
}

void PrintState::onEntry(QEvent *event)
{
    QStateMachine::SignalEvent *e = dynamic_cast<QStateMachine::SignalEvent*>(event);

    if (e && !e->arguments().isEmpty()) {
        const QString command = e->arguments()[0].toString();
        QTextStream stream(stdout);
        stream << command << "\n";
    }

    emit completed();
}

static QStringList tab_completion_full_state;
static bool tab_completion_at_root = false;

static char **akonadi2_cli_tab_completion(const char *text, int start, int end)
{
    tab_completion_at_root = start == 0;
    tab_completion_full_state = QString(rl_line_buffer).remove(start, end - start).split(" ", QString::SkipEmptyParts);
    return NULL;
}

static char *akonadi2_cli_next_tab_complete_match(const char *text, int state)
{
    Syntax::List nearest = SyntaxTree::self()->nearestSyntax(tab_completion_full_state, QString(text));

    if (nearest.size() > state) {
        return qstrdup(nearest[state].keyword.toUtf8());
    }

    return rl_filename_completion_function(text, state);
}

#include "moc_replStates.cpp"
