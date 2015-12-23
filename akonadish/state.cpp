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

#include "state.h"

#include <QDebug>
#include <QEventLoop>
#include <QTextStream>

class State::Private
{
public:
    Private()
        : outStream(stdout)
    {
    }

    int debugLevel = 0;
    QEventLoop eventLoop;
    QTextStream outStream;
};

State::State()
    : d(new Private)
{
}

void State::print(const QString &message, unsigned int indentationLevel) const
{
    for (unsigned int i = 0; i < indentationLevel; ++i) {
        d->outStream << "\t";
    }

    d->outStream << message;
}

void State::printLine(const QString &message, unsigned int indentationLevel) const
{
    print(message, indentationLevel);
    d->outStream << "\n";
    d->outStream.flush();
}

void State::printError(const QString &errorMessage, const QString &errorCode) const
{
    printLine("ERROR" + (errorCode.isEmpty() ? "" : " " + errorCode) + ": " + errorMessage);
}

void State::setDebugLevel(unsigned int level)
{
    if (level < 7) {
        d->debugLevel = level;
    }
}

unsigned int State::debugLevel() const
{
    return d->debugLevel;
}

int State::commandStarted() const
{
    if (!d->eventLoop.isRunning()) {
        //qDebug() << "RUNNING THE EVENT LOOP!";
        return d->eventLoop.exec();
    }

    return 0;
}

void State::commandFinished(int returnCode) const
{
    d->eventLoop.exit(returnCode);
}

