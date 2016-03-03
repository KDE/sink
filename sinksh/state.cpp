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

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTextStream>

#include "common/log.h"

static bool s_hasEventLoop = false;

class State::Private
{
public:
    Private() : outStream(stdout)
    {
    }

    QEventLoop *eventLoop()
    {
        if (!event) {
            event = new QEventLoop;
        }

        return event;
    }

    int debugLevel = 0;
    QEventLoop *event = 0;
    bool timing = false;
    QTextStream outStream;
};

State::State() : d(new Private)
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
    if (!s_hasEventLoop) {
        return QCoreApplication::exec();
    } else if (!d->eventLoop()->isRunning()) {
        return d->eventLoop()->exec();
    }

    return 0;
}

void State::commandFinished(int returnCode) const
{
    if (!s_hasEventLoop) {
        QCoreApplication::exit(returnCode);
    } else {
        d->eventLoop()->exit(returnCode);
    }
}

void State::setHasEventLoop(bool evented)
{
    s_hasEventLoop = evented;
}

bool State::hasEventLoop()
{
    return s_hasEventLoop;
}

void State::setCommandTiming(bool time)
{
    d->timing = time;
}

bool State::commandTiming() const
{
    return d->timing;
}

void State::setLoggingLevel(const QString &level) const
{
    Sink::Log::setDebugOutputLevel(Sink::Log::debugLevelFromName(level.toLatin1()));
}

QString State::loggingLevel() const
{
    // do not turn this into a single line return: that core dumps due to allocation of
    // the byte array in Sink::Log
    QByteArray rv = Sink::Log::debugLevelName(Sink::Log::debugOutputLevel());
    return rv.toLower();
}
