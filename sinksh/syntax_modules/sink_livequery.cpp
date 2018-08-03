/*
 *   Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
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
#include "common/definitions.h"
#include "common/store.h"
#include "common/propertyparser.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkLiveQuery
{

Syntax::List syntax();

bool livequery(const QStringList &args_, State &state)
{
    if (args_.isEmpty()) {
        state.printError(syntax()[0].usage());
        return false;
    }

    auto options = SyntaxTree::parseOptions(args_);

    auto type = options.positionalArguments.isEmpty() ? QString{} : options.positionalArguments.first();

    bool asLine = true;

    Sink::Query query;
    query.setId("livequery");
    query.setFlags(Sink::Query::LiveQuery);
    if (!SinkshUtils::applyFilter(query, options)) {
        state.printError(syntax()[0].usage());
        return false;
    }
    if (options.options.contains("resource")) {
        for (const auto &f : options.options.value("resource")) {
            query.resourceFilter(f.toLatin1());
        }
    }
    if (options.options.contains("filter")) {
        for (const auto &f : options.options.value("filter")) {
            auto filter = f.split("=");
            const auto property = filter.value(0).toLatin1();
            query.filter(property, Sink::PropertyParser::parse(type.toLatin1(), property, filter.value(1)));
        }
    }
    if (options.options.contains("id")) {
        for (const auto &f : options.options.value("id")) {
            query.filter(f.toUtf8());
        }
    }
    // auto compact = options.options.contains("compact");
    if (!options.options.contains("showall")) {
        if (options.options.contains("show")) {
            auto list = options.options.value("show");
            std::transform(list.constBegin(), list.constEnd(), std::back_inserter(query.requestedProperties), [] (const QString &s) { return s.toLatin1(); });
        } else {
            query.requestedProperties = SinkshUtils::requestedProperties(type);
        }
    } else {
        asLine = false;
    }

    QByteArrayList toPrint;
    QStringList tableLine;

    auto model = SinkshUtils::loadModel(query.type(), query);
    QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [model, state](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
        if (roles.contains(Sink::Store::ChildrenFetchedRole)) {
            state.printLine(QObject::tr("Counted results %1").arg(model->rowCount(QModelIndex())));
        }
    });
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

    if (!model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool()) {
        return true;
    }

    return false;
}

Syntax::List syntax()
{
    Syntax list("livequery", QObject::tr("Run a livequery."), &SinkLiveQuery::livequery, Syntax::EventDriven);

    list.addPositionalArgument({ .name = "type", .help = "The type to run the livequery on" });
    list.addParameter("resource", { .name = "resource", .help = "Filter the livequery to the given resource" });
    list.addFlag("compact", "Use a compact view (reduces the size of IDs)");
    list.addParameter("filter", { .name = "property=$value", .help = "Filter the results" });
    list.addParameter("id", { .name = "id", .help = "List only the content with the given ID" });
    list.addFlag("showall", "Show all properties");
    list.addParameter("show", { .name = "property", .help = "Only show the given property" });

    list.completer = &SinkshUtils::resourceOrTypeCompleter;
    return Syntax::List() << list;
}

REGISTER_SYNTAX(SinkLiveQuery)

}
