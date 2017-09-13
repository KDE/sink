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

#include <QDebug>
#include <QObject> // tr()
#include <QModelIndex>
#include <QTime>

#include "common/storage.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkShow
{

bool show(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("Options: $type --resource $resource --id $id"));
        return false;
    }

    auto options = SyntaxTree::parseOptions(args);

    auto type = options.positionalArguments.isEmpty() ? QString{} : options.positionalArguments.first();
    auto resource = options.options.value("resource");
    auto id = options.options.value("id");

    if (id.isEmpty() || resource.isEmpty() || !SinkshUtils::isValidStoreType(type)) {
        state.printError(QObject::tr("Invalid command syntax. Supply type and resource at least."));
        return false;
    }

    Sink::Query query;
    query.resourceFilter(resource.first().toLatin1());
    query.filter(id.first().toLatin1());

    QTime time;
    time.start();
    auto model = SinkshUtils::loadModel(type, query);
    if (state.debugLevel() > 0) {
        state.printLine(QObject::tr("Folder type %1").arg(type));
        state.printLine(QObject::tr("Loaded model in %1 ms").arg(time.elapsed()));
    }

    QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, [model, state](const QModelIndex &index, int start, int end) {
        for (int i = start; i <= end; i++) {
            auto object = model->data(model->index(i, 0, index), Sink::Store::DomainObjectBaseRole).value<Sink::ApplicationDomain::ApplicationDomainType::Ptr>();
            state.printLine("Resource: " + object->resourceInstanceIdentifier(), 1);
            state.printLine("Identifier: " + object->identifier(), 1);
            state.stageTableLine(QStringList()
                    << QObject::tr("Property:")
                    << QObject::tr("Value:"));

            for (const auto &property : object->availableProperties()) {
                state.stageTableLine(QStringList()
                        << property
                        << object->getProperty(property).toString());
            }
            state.flushTable();
        }
    });

    QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [model, state](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
        if (roles.contains(Sink::Store::ChildrenFetchedRole)) {
            state.commandFinished();
        }
    });

    if (!model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool()) {
        return true;
    }

    return false;
}

Syntax::List syntax()
{
    Syntax show("show", QObject::tr("Show all properties of an entity."), &SinkShow::show, Syntax::EventDriven);
    show.completer = &SinkshUtils::resourceOrTypeCompleter;
    return Syntax::List() << show;
}

REGISTER_SYNTAX(SinkShow)

}
