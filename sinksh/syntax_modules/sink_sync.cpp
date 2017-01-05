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
#include "common/domain/event.h"
#include "common/domain/folder.h"
#include "common/resourceconfig.h"
#include "common/log.h"
#include "common/storage.h"
#include "common/definitions.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkSync
{

bool isId(const QByteArray &value)
{
    return value.startsWith("{");
}

bool sync(const QStringList &args_, State &state)
{
    auto args = args_;
    Sink::Query query;
    if (args.isEmpty()) {
        state.printError(QObject::tr("Options: $type $resource/$folder/$subfolder"));
        return false;
    }
    auto type = args.takeFirst().toLatin1();
    if (type != "*") {
        query.setType(type);
    }
    if (!args.isEmpty()) {
        auto resource = args.takeFirst().toLatin1();

        if (resource.contains('/')) {
            //The resource isn't an id but a path
            auto list = resource.split('/');
            const auto resourceId = list.takeFirst();
            query.resourceFilter(resourceId);
            if (type == Sink::ApplicationDomain::getTypeName<Sink::ApplicationDomain::Mail>() && !list.isEmpty()) {
                auto value = list.takeFirst();
                if (isId(value)) {
                    query.filter<Sink::ApplicationDomain::Mail::Folder>(value);
                } else {
                    Sink::Query folderQuery;
                    folderQuery.resourceFilter(resourceId);
                    folderQuery.filter<Sink::ApplicationDomain::Folder::Name>(value);
                    folderQuery.filter<Sink::ApplicationDomain::Folder::Parent>(QVariant());
                    qWarning() << "Looking for folder: " << value << " in " << resourceId;
                    auto folders = Sink::Store::read<Sink::ApplicationDomain::Folder>(folderQuery);
                    if (folders.size() == 1) {
                        query.filter<Sink::ApplicationDomain::Mail::Folder>(folders.first());
                        qWarning() << "Synchronizing folder: " << folders.first().identifier();
                    } else {
                        qWarning() << "Folder name did not match uniquely: " << folders.size();
                        for (const auto &f : folders) {
                            qWarning() << f.getName();
                        }
                        state.printError(QObject::tr("Folder name did not match uniquely."));
                    }
                }
            }
        } else {
            query.resourceFilter(resource);
        }
    }

    QTimer::singleShot(0, [query, state]() {
    Sink::Store::synchronize(query)
        .then(Sink::ResourceControl::flushMessageQueue(query.getResourceFilter().ids))
        .syncThen<void>([state](const KAsync::Error &error) {
            if (error) {
                state.printLine("Synchronization failed!");
            } else {
                state.printLine("Synchronization complete!");
            }
            state.commandFinished();
        }).exec();
    });

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
