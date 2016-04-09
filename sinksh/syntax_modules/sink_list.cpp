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

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkList
{

bool list(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("Please provide at least one type to list (e.g. resource, .."));
        return false;
    }

    auto resources = args;
    auto type = !resources.isEmpty() ? resources.takeFirst() : QString();

    if (!type.isEmpty() && !SinkshUtils::isValidStoreType(type)) {
        state.printError(QObject::tr("Unknown type: %1").arg(type));
        return false;
    }

    Sink::Query query;
    for (const auto &res : resources) {
        query.resources << res.toLatin1();
    }
    query.liveQuery = false;

    QTime time;
    time.start();
    auto model = SinkshUtils::loadModel(type, query);
    if (state.debugLevel() > 0) {
        state.printLine(QObject::tr("Folder type %1").arg(type));
        state.printLine(QObject::tr("Loaded model in %1 ms").arg(time.elapsed()));
    }

    //qDebug() << "Listing";
    QStringList line;
    line << QObject::tr("Resource") << QObject::tr("Identifier");
    for (int i = 0; i < model->columnCount(QModelIndex()); i++) {
        line << model->headerData(i, Qt::Horizontal).toString();
    }
    state.stageTableLine(line);

    QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, [model, state](const QModelIndex &index, int start, int end) {
        for (int i = start; i <= end; i++) {
            auto object = model->data(model->index(i, 0, index), Sink::Store::DomainObjectBaseRole).value<Sink::ApplicationDomain::ApplicationDomainType::Ptr>();
            QStringList line;
            line << object->resourceInstanceIdentifier();
            line << object->identifier();
            for (int col = 0; col < model->columnCount(QModelIndex()); col++) {
                line << model->data(model->index(i, col, index)).toString();
            }
            state.stageTableLine(line);
        }
    });

    QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [model, state](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
        if (roles.contains(Sink::Store::ChildrenFetchedRole)) {
            state.flushTable();
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
    Syntax list("list", QObject::tr("List all resources, or the contents of one or more resources"), &SinkList::list, Syntax::EventDriven);
    list.completer = &SinkshUtils::resourceOrTypeCompleter;
    return Syntax::List() << list;
}

REGISTER_SYNTAX(SinkList)

}
