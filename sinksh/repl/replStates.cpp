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

#include <iostream>

#include <QDebug>
#include <QEvent>
#include <QStateMachine>

#include "commandline.h"
#include "syntaxtree.h"

ReadState::ReadState(QState *parent)
    : QState(parent)
{
    Commandline::setCompletionCallback([](const char* editBuffer, std::vector<std::string>& completions) {
        QStringList words = QString(editBuffer).split(" ", QString::SkipEmptyParts);
        const QString fragment = words.takeLast();
        Syntax::List nearest = SyntaxTree::self()->nearestSyntax(words, fragment);
        if (nearest.isEmpty()) {
            SyntaxTree::Command command = SyntaxTree::self()->match(words);
            if (command.first && command.first->completer) {
                QStringList commandCompletions = command.first->completer(words, fragment, SyntaxTree::self()->state());
                for (const auto &c : commandCompletions) {
                    completions.push_back(c.toStdString());
                }
            }
        } else {
            for (const auto &n : nearest) {
                completions.push_back(n.keyword.toStdString());
            }
        }
    });
}

void ReadState::onEntry(QEvent *event)
{
    Q_UNUSED(event)

    std::string line;
    if (Commandline::readline(prompt(), line)) {
        std::cout << std::endl;
        emit exitRequested();
        return;
    }

    // we have actual data, so let's wait for a full line of text
    const QString text = QString::fromStdString(line).simplified();

    if (text.length() > 0) {
        Commandline::addHistory(line);
    }

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


//Ignore warning I don't know how to fix in a moc file
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "moc_replStates.cpp"
#pragma clang diagnostic pop
