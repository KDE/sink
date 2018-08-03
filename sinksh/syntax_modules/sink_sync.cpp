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
#include "common/secretstore.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkSync
{

bool sync(const QStringList &args, State &state)
{
    auto options = SyntaxTree::parseOptions(args);
    if (options.options.value("password").isEmpty()) {
        state.printError(QObject::tr("Pass in a password with --password"));
        return false;
    }
    auto password = options.options.value("password").first();

    Sink::Query query;
    if (!options.positionalArguments.isEmpty() && !SinkshUtils::isValidStoreType(options.positionalArguments.first())) {
        //We have only specified a resource
        query.resourceFilter(SinkshUtils::parseUid(options.positionalArguments.first().toLatin1()));
    } else {
        //We have specified a full filter
        if (!SinkshUtils::applyFilter(query, options.positionalArguments)) {
            state.printError(QObject::tr("Options: $type $resource/$folder/$subfolder --password $password"));
            return false;
        }
    }
    if (query.getResourceFilter().ids.isEmpty()) {
        state.printError(QObject::tr("Failed to find resource filter"));
        return false;
    }
    auto resourceId = query.getResourceFilter().ids.first();
    Sink::SecretStore::instance().insert(resourceId, password);

    Sink::Store::synchronize(query)
        .then(Sink::ResourceControl::flushMessageQueue(query.getResourceFilter().ids))
        .then([state](const KAsync::Error &error) {
            int exitCode = 0;
            if (error) {
                state.printLine("Synchronization failed!");
                exitCode = 1;
            } else {
                state.printLine("Synchronization complete!");
            }
            state.commandFinished(exitCode);
        }).exec();

    return true;
}

Syntax::List syntax()
{
    Syntax sync("sync", QObject::tr("Synchronizes a resource."), &SinkSync::sync, Syntax::EventDriven);

    sync.addPositionalArgument({ .name = "type", .help = "The type of resource to synchronize" });
    sync.addPositionalArgument({ .name = "resourceId", .help = "The ID of the resource to synchronize" });
    sync.addParameter("password", { .name = "password", .help = "The password of the resource", .required = true });

    sync.completer = &SinkshUtils::resourceCompleter;

    return Syntax::List() << sync;
}

REGISTER_SYNTAX(SinkSync)

}
