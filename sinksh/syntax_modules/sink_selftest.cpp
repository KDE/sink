/*
 *   Copyright (C) 2018 Christian Mollekopf <mollekopf@kolabsys.com>
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
#include "common/store.h"
#include "common/propertyparser.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkSelfTest
{

bool selfTest(const QStringList &args_, State &state)
{
    if (args_.isEmpty()) {
        state.printError(QObject::tr("Options: $type [--resource $resource] stresstest"));
        return false;
    }

    auto options = SyntaxTree::parseOptions(args_);
    if (options.positionalArguments.contains("stresstest")) {
        auto resource = SinkshUtils::parseUid(options.options.value("resource").first().toUtf8());
        qWarning() << "Stresstest on resource: " << resource;
        Sink::Query query;
        query.resourceFilter(resource);
        query.limit(100);

        auto models = QSharedPointer<QList<QSharedPointer<QAbstractItemModel>>>::create();
        for (int i = 0; i < 50; i++) {
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
            *models << model;
            QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [models, model, &state](const QModelIndex &start, const QModelIndex &end, const QVector<int> &roles) {
                if (roles.contains(Sink::Store::ChildrenFetchedRole)) {
                    models->removeAll(model);
                    qWarning() << "Model complete: " << models->count();
                    if (models->isEmpty()) {
                        state.commandFinished();
                    }
                }
                    });
        }
        return true;
    }
    return false;
}

Syntax::List syntax()
{
    Syntax syntax("selftest", QObject::tr("Selftext."), &SinkSelfTest::selfTest, Syntax::EventDriven);
    return Syntax::List() << syntax;
}

REGISTER_SYNTAX(SinkSelfTest)

}
