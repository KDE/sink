/*
 *   Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *   Copyright (C) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
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

#include <QCoreApplication>
#include <QDebug>
#include <QObject> // tr()
#include <QDir>
#include <QDirIterator>

#include "common/log.h"
#include "common/storage.h"
#include "common/definitions.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkDrop
{

bool drop(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("Please provide at least one resource to drop."));
        return false;
    }

    auto argList = args;
    auto resource = argList.takeFirst();

    QDirIterator it(Sink::storageLocation(), QStringList() << resource + "*", QDir::Dirs);
    while (it.hasNext()) {
        auto path = it.next();
        QDir dir(path);
        state.printLine("Removing: " + path, 1);
        if (!dir.removeRecursively()) {
            state.printError(QObject::tr("Failed to remove: ") + dir.path());
        }
    }

    return false;
}

Syntax::List syntax()
{
    Syntax drop("drop", QObject::tr("Drop all caches of a resource."), &SinkDrop::drop, Syntax::NotInteractive);
    drop.completer = &SinkshUtils::resourceOrTypeCompleter;
    return Syntax::List() << drop;
}

REGISTER_SYNTAX(SinkDrop)

}
