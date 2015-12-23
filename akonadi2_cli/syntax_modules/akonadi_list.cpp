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

#include "akonadi_list.h"

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

namespace AkonadiList
{

Syntax::List syntax()
{
    Syntax::List syntax;
    syntax << Syntax("list", QObject::tr("List all resources, or the contents of one or more resources"), &AkonadiList::list, Syntax::EventDriven);

    return syntax;
}

bool list(const QStringList &args, State &state)
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

    QTime time;
    time.start();
    auto model = AkonadishUtils::loadModel(type, query);
    if (state.debugLevel() > 0) {
        state.printLine(QObject::tr("Folder type %1").arg(type));
        state.printLine(QObject::tr("Loaded model in %1 ms").arg(time.elapsed()));
    }

    //qDebug() << "Listing";
    int colSize = 38; //Necessary to display a complete UUID
    state.print("  " + QObject::tr("Column") + "     ");
    state.print(QObject::tr("Resource").leftJustified(colSize, ' ', true) +
                QObject::tr("Identifier").leftJustified(colSize, ' ', true));
    for (int i = 0; i < model->columnCount(QModelIndex()); i++) {
        state.print(" | " + model->headerData(i, Qt::Horizontal).toString().leftJustified(colSize, ' ', true));
    }
    state.printLine();

    QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, [model, colSize, state](const QModelIndex &index, int start, int end) {
        for (int i = start; i <= end; i++) {
            state.print("  " + QObject::tr("Row %1").arg(QString::number(model->rowCount())).rightJustified(4, ' ') + ": ");
            auto object = model->data(model->index(i, 0, index), Akonadi2::Store::DomainObjectBaseRole).value<Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr>();
            state.print("  " + object->resourceInstanceIdentifier().leftJustified(colSize, ' ', true));
            state.print(object->identifier().leftJustified(colSize, ' ', true));
            for (int col = 0; col < model->columnCount(QModelIndex()); col++) {
                state.print(" | " + model->data(model->index(i, col, index)).toString().leftJustified(colSize, ' ', true));
            }
            state.printLine();
        }
    });

    QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [model, state](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
        if (roles.contains(Akonadi2::Store::ChildrenFetchedRole)) {
            state.commandFinished();
        }
    });

    if (!model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool()) {
        return true;
    }

    return false;
}

}
