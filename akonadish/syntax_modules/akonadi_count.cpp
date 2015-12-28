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

#include <QCoreApplication>
#include <QDebug>
#include <QObject> // tr()
#include <QModelIndex>
#include <QTime>

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

namespace AkonadiCount
{

bool count(const QStringList &args, State &state)
{
    auto resources = args;
    auto type = !resources.isEmpty() ? resources.takeFirst() : QString();

    if (!type.isEmpty() && !AkonadishUtils::isValidStoreType(type)) {
        state.printError(QObject::tr("Unknown type: %1").arg(type));
        return false;
    }

    Akonadi2::Query query;
    for (const auto &res : resources) {
        query.resources << res.toLatin1();
    }
    query.syncOnDemand = false;
    query.processAll = false;
    query.liveQuery = false;

    auto model = AkonadishUtils::loadModel(type, query);
    QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [model, state](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
        if (roles.contains(Akonadi2::Store::ChildrenFetchedRole)) {
            state.printLine(QObject::tr("Counted results %1").arg(model->rowCount(QModelIndex())));
            state.commandFinished();
        }
    });

    if (!model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool()) {
        return true;
    }

    return true;
}

Syntax::List syntax()
{
    Syntax count("count", QObject::tr("Returns the number of items of a given type in a resource. Usage: count <type> <resource>"), &AkonadiCount::count, Syntax::EventDriven);
    count.completer = &AkonadishUtils::typeCompleter;

    return Syntax::List() << count;
}

REGISTER_SYNTAX(AkonadiCount)

}
