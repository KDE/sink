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

#include <unistd.h>

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTextStream>

#include "syntaxtree.h"
// #include "jsonlistener.h"
#include "repl/repl.h"

/*
 * modes of operation:
 *
 *   1. called with no commands: start the REPL
 *   2. called with -: listen for commands on stdin
 *   3. called with a filename: try to run it as a script
 *   4. called with commands: try to match to syntax and run the result
 */

int enterRepl()
{
    if (State::hasEventLoop()) {
        return 0;
    }

    Repl *repl = new Repl;
    QObject::connect(repl, &QStateMachine::finished, repl, &QObject::deleteLater);
    QObject::connect(repl, &QStateMachine::finished, QCoreApplication::instance(), &QCoreApplication::quit);

    State::setHasEventLoop(true);
    int rv = QCoreApplication::instance()->exec();
    State::setHasEventLoop(false);
    return rv;
}

bool goInteractive(const QStringList &, State &)
{
    enterRepl();
    return true;
}

Syntax::List goInteractiveSyntax()
{
    Syntax interactive("go_interactive", QString(), &goInteractive);
    return Syntax::List() << interactive;
}

void processCommandStream(QTextStream &stream)
{
    SyntaxTree::self()->registerSyntax(&goInteractiveSyntax);
    QString line = stream.readLine();
    while (!line.isEmpty()) {
        line = line.trimmed();

        if (!line.isEmpty() && !line.startsWith('#')) {
            SyntaxTree::self()->run(SyntaxTree::tokenize(line));
        }

        line = stream.readLine();
    }
}

int main(int argc, char *argv[])
{
    const bool interactive = isatty(fileno(stdin));
    const bool startRepl = (argc == 1) && interactive;
    // TODO: make a json command parse cause that would be awesomesauce
    const bool fromScript = !startRepl && QFile::exists(argv[1]);

    // qDebug() << "state at startup is" << interactive << startRepl << fromScript;

    QCoreApplication app(argc, argv);
    app.setApplicationName(fromScript ? "interactive-app-shell" : argv[0]);

    if (startRepl) {
        return enterRepl();
    } else if (fromScript) {
        QFile f(argv[1]);
        if (!f.open(QIODevice::ReadOnly)) {
            return 1;
        }

        QTextStream inputStream(&f);
        processCommandStream(inputStream);
    } else if (!interactive) {
        QTextStream inputStream(stdin);
        processCommandStream(inputStream);
    } else {
        QStringList commands = app.arguments();
        commands.removeFirst();
        return SyntaxTree::self()->run(commands);
    }
}
