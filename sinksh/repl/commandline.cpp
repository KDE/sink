/*
 *   Copyright (C) 2018 Christian Mollekopf <mollekopf@kolabsys.com>
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
#include "commandline.h"

#include "linenoise.hpp"

void Commandline::loadHistory(const QString &path)
{
    linenoise::LoadHistory(path.toLocal8Bit());
}

void Commandline::saveHistory(const QString &path)
{
    linenoise::SaveHistory(path.toLocal8Bit());
}

void Commandline::addHistory(const std::string &line)
{
    linenoise::AddHistory(line.c_str());
}

void Commandline::setCompletionCallback(std::function<void (const char*, std::vector<std::string>&)> callback)
{
    linenoise::SetCompletionCallback(callback);
}

bool Commandline::readline(const char *prompt, std::string &line)
{
    return linenoise::Readline(prompt, line);
}
