/*
 *   Copyright (C) 2021 Christian Mollekopf <christian@mkpf.ch>
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
#include <iostream>

#include "common/log.h"
#include "common/notification.h"
#include "common/notifier.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkMonitor
{

Syntax::List syntax();

bool monitor(const QStringList &args_, State &state)
{
    if (args_.isEmpty()) {
        state.printError(syntax()[0].usage());
        return false;
    }

    auto options = SyntaxTree::parseOptions(args_);

    Sink::Query query;
    query.setId("monitor");
    if (options.options.contains("resource")) {
        for (const auto &f : options.options.value("resource")) {
            query.resourceFilter(f.toLatin1());
        }
    }
    auto notifier = new Sink::Notifier{query};

    notifier->registerHandler([&] (const Sink::Notification &notification) {
        SinkLog() << "Received notification: " << notification;
    });


    return true;
}

Syntax::List syntax()
{
    Syntax resource("monitor", QObject::tr("Monitor resource status."), &SinkMonitor::monitor, Syntax::EventDriven);

    resource.addParameter("resource", {"resource", "Resource to monitor" });

    resource.completer = &SinkshUtils::resourceOrTypeCompleter;
    return {resource};
}

REGISTER_SYNTAX(SinkMonitor)

}
