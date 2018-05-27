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
#pragma once

#include <QString>
#include <functional>
#include <vector>

/*
 * Wrapper for linenoise.
 *
 * Because global variables in header files don't work when included from multiple places.
 */
namespace Commandline {
    void loadHistory(const QString &);
    void saveHistory(const QString &);
    void addHistory(const std::string &);
    void setCompletionCallback(std::function<void (const char*, std::vector<std::string>&)>);
    bool readline(const char *prompt, std::string &line);
};
