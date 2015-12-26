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

#include "repl.h"

#include <readline/history.h>

#include <QDir>
#include <QFile>
#include <QFinalState>
#include <QStandardPaths>
#include <QTextStream>

#include "replStates.h"
#include "syntaxtree.h"

Repl::Repl(QObject *parent)
    : QStateMachine(parent)
{
    // readline history setup
    using_history();
    read_history(commandHistoryPath().toLocal8Bit());

    // create all states
    ReadState *read = new ReadState(this);
    UnfinishedReadState *unfinishedRead = new UnfinishedReadState(this);
    EvalState *eval = new EvalState(this);
    PrintState *print = new PrintState(this);
    QFinalState *final = new QFinalState(this);

    // connect the transitions
    read->addTransition(read, SIGNAL(command(QString)), eval);
    read->addTransition(read, SIGNAL(exitRequested()), final);

    unfinishedRead->addTransition(unfinishedRead, SIGNAL(command(QString)), eval);
    unfinishedRead->addTransition(unfinishedRead, SIGNAL(exitRequested()), final);

    eval->addTransition(eval, SIGNAL(completed()), read);
    eval->addTransition(eval, SIGNAL(continueInput()), unfinishedRead);
    eval->addTransition(eval, SIGNAL(output(QString)), print);

    print->addTransition(print, SIGNAL(completed()), eval);

    setInitialState(read);
    printWelcomeBanner();
    start();
}

Repl::~Repl()
{
    // readline history writing
    write_history(commandHistoryPath().toLocal8Bit());
}

void Repl::printWelcomeBanner()
{
    QTextStream out(stdout);
    out << QObject::tr("Welcome to the Akonadi2 interative shell!\n");
    out << QObject::tr("Type `help` for information on the available commands.\n");
    out.flush();
}

QString Repl::commandHistoryPath()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::DataLocation);

    if (!QFile::exists(path)) {
        QDir dir;
        dir.mkpath(path);
    }

    return  path + "/repl_history";
}

#include "moc_repl.cpp"
