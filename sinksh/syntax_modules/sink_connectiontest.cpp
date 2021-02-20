/*
 *   Copyright (C) 2020 Christian Mollekopf <mollekopf@kolabsystems.com>
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
#include "common/resourcecontrol.h"
#include "common/log.h"
#include "common/definitions.h"
#include "common/secretstore.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkConnectionTest
{

bool connectiontest(const QStringList &args, State &state)
{
    auto options = SyntaxTree::parseOptions(args);
    if (options.options.value("password").isEmpty()) {
        state.printError(QObject::tr("Pass in a password with --password"));
        return false;
    }
    auto password = options.options.value("password").first();

    Sink::Query query;
    auto resourceId = SinkshUtils::parseUid(options.positionalArguments.first().toLatin1());

    Sink::SecretStore::instance().insert(resourceId, password);

    Sink::ResourceControl::inspect(Sink::ResourceControl::Inspection::ConnectionInspection(resourceId))
        .then([state](const KAsync::Error &error) {
            int exitCode = 0;
            if (error) {
                state.printLine("Connection test failed!");
                exitCode = 1;
            } else {
                state.printLine("Connection test successful!");
            }
            state.commandFinished(exitCode);
        }).exec();

    return true;
}

Syntax::List syntax()
{
    Syntax connectiontest("connectiontest", QObject::tr("Test the connection to a server."), &SinkConnectionTest::connectiontest, Syntax::EventDriven);

    connectiontest.addPositionalArgument({"resourceId", "The ID of the resource to synchronize"});
    connectiontest.addParameter("password", {"password", "The password of the resource", true});

    connectiontest.completer = &SinkshUtils::resourceCompleter;

    return Syntax::List() << connectiontest;
}

REGISTER_SYNTAX(SinkConnectionTest)

}
