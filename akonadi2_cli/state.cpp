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
#include <QTextStream>

State::State()
    : m_outStream(stdout)
{
}

void State::print(const QString &message, unsigned int indentationLevel)
{
    for (unsigned int i = 0; i < indentationLevel; ++i) {
        m_outStream << "\t";
    }

    m_outStream << message;
}

void State::printLine(const QString &message, unsigned int indentationLevel)
{
    print(message, indentationLevel);
    m_outStream << "\n";
    m_outStream.flush();
}

void State::printError(const QString &errorMessage, const QString &errorCode)
{
    printLine("ERROR" + (errorCode.isEmpty() ? "" : " " + errorCode) + ": " + errorMessage);
}

void State::setDebugLevel(unsigned int level)
{
    if (level < 7) {
        m_debugLevel = level;
    }
}

unsigned int State::debugLevel() const
{
    return m_debugLevel;
}

