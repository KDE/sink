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

#include <QDebug>
#include <QObject> // tr()
#include <QTimer>

#include "common/resource.h"
#include "common/storage.h"
#include "common/domain/event.h"
#include "common/domain/folder.h"
#include "common/resourceconfig.h"
#include "common/log.h"
#include "common/storage.h"
#include "common/definitions.h"

#include "akonadish_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace AkonadiClear
{

bool clear(const QStringList &args, State &state)
{
    for (const auto &resource : args) {
        state.print(QObject::tr("Removing local cache for '%1' ...").arg(resource));
        Akonadi2::Store::removeFromDisk(resource.toLatin1());
        state.printLine(QObject::tr("done"));
    }

    return true;
}

Syntax::List syntax()
{
    Syntax clear("clear", QObject::tr("Clears the local cache of one or more resources (be careful!)"), &AkonadiClear::clear);
    clear.completer = &AkonadishUtils::resourceCompleter;

    return Syntax::List() << clear;
}

REGISTER_SYNTAX(AkonadiClear)

}
