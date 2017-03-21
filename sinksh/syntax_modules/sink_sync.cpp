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
#include "common/resourcecontrol.h"
#include "common/resourceconfig.h"
#include "common/log.h"
#include "common/storage.h"
#include "common/definitions.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkSync
{

bool sync(const QStringList &args, State &state)
{
    Sink::Query query;
    
    if (!args.isEmpty() && !SinkshUtils::isValidStoreType(args.first())) {
        query.resourceFilter(args.first().toLatin1());
    } else {
        if (!SinkshUtils::applyFilter(query, args)) {
            state.printError(QObject::tr("Options: $type $resource/$folder/$subfolder"));
            return false;
        }
    }

    Sink::Store::synchronize(query)
        .then(Sink::ResourceControl::flushMessageQueue(query.getResourceFilter().ids))
        .then([state](const KAsync::Error &error) {
            if (error) {
                state.printLine("Synchronization failed!");
            } else {
                state.printLine("Synchronization complete!");
            }
            state.commandFinished();
        }).exec();

    return true;
}

Syntax::List syntax()
{
    Syntax sync("sync", QObject::tr("Syncronizes all resources that are listed; and empty list triggers a syncronizaton on all resources"), &SinkSync::sync, Syntax::EventDriven);
    sync.completer = &SinkshUtils::resourceCompleter;

    return Syntax::List() << sync;
}

REGISTER_SYNTAX(SinkSync)

}
