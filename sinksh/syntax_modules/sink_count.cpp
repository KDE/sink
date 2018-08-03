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
#include "common/resourceconfig.h"
#include "common/log.h"
#include "common/storage.h"
#include "common/definitions.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkCount
{

Syntax::List syntax();

bool count(const QStringList &args, State &state)
{
    Sink::Query query;
    query.setId("count");
    if (!SinkshUtils::applyFilter(query, SyntaxTree::parseOptions(args))) {
        state.printError(syntax()[0].usage());
        return false;
    }

    auto model = SinkshUtils::loadModel(query.type(), query);
    QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [model, state](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
        if (roles.contains(Sink::Store::ChildrenFetchedRole)) {
            state.printLine(QObject::tr("Counted results %1").arg(model->rowCount(QModelIndex())));
            state.commandFinished();
        }
    });

    if (!model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool()) {
        return true;
    }

    return true;
}

Syntax::List syntax()
{
    Syntax count("count", QObject::tr("Returns the number of items of a given type in a resource"), &SinkCount::count, Syntax::EventDriven);

    count.addPositionalArgument({.name = "type", .help = "The entity type to count"});
    count.addPositionalArgument({.name = "resource", .help = "A resource id where to count", .required = false});

    count.completer = &SinkshUtils::typeCompleter;

    return Syntax::List() << count;
}

REGISTER_SYNTAX(SinkCount)

}
