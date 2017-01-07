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
#include "common/store.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

SINK_DEBUG_AREA("sinksh_list")

namespace SinkList
{

static QByteArray compressId(bool compress, const QByteArray &id)
{
    if (!compress) {
        return id;
    }
    auto compactId = id.mid(1, id.length() - 2).split('-');
    if (compactId.isEmpty()) {
        //Failed to compress id
        return id;
    }
    return compactId.first();
}

QByteArray baIfAvailable(const QStringList &list)
{
    if (list.isEmpty()) {
        return QByteArray{};
    }
    return list.first().toUtf8();
}

bool list(const QStringList &args_, State &state)
{
    if (args_.isEmpty()) {
        state.printError(QObject::tr("Options: $type [--resource $resource] [--compact] [--filter $property=$value] [--showall|--show $property]"));
        return false;
    }

    auto options = SyntaxTree::parseOptions(args_);

    auto type = options.positionalArguments.isEmpty() ? QString{} : options.positionalArguments.first();

    Sink::Query query;
    query.setId("list");
    if (!SinkshUtils::applyFilter(query, options)) {
        state.printError(QObject::tr("Options: $type [--resource $resource] [--compact] [--filter $property=$value] [--showall|--show $property]"));
        return false;
    }
    if (options.options.contains("filter")) {
        for (const auto &f : options.options.value("filter")) {
            auto filter = f.split("=");
            query.filter(filter.at(0).toLatin1(), QVariant::fromValue(Sink::ApplicationDomain::Reference{filter.at(1).toLatin1()}));
        }
    }
    auto compact = options.options.contains("compact");
    if (!options.options.contains("showall")) {
        if (options.options.contains("show")) {
            auto list = options.options.value("show");
            std::transform(list.constBegin(), list.constEnd(), std::back_inserter(query.requestedProperties), [] (const QString &s) { return s.toLatin1(); });
        } else {
            query.requestedProperties = SinkshUtils::requestedProperties(type);
        }
    }

    QStringList tableLine;
    QByteArrayList toPrint;

    for (const auto &o : SinkshUtils::getStore(type).read(query)) {
        if (tableLine.isEmpty()) {
            tableLine << QObject::tr("Resource") << QObject::tr("Identifier");
            if (query.requestedProperties.isEmpty()) {
                auto in = o.availableProperties();
                toPrint = o.availableProperties();
                std::transform(in.constBegin(), in.constEnd(), std::back_inserter(tableLine), [] (const QByteArray &s) -> QString { return s; });
            } else {
                auto in = query.requestedProperties;
                toPrint = query.requestedProperties;
                std::transform(in.constBegin(), in.constEnd(), std::back_inserter(tableLine), [] (const QByteArray &s) -> QString { return s; });
            }
            state.stageTableLine(tableLine);
        }

        QStringList line;
        line << compressId(compact, o.resourceInstanceIdentifier());
        line << compressId(compact, o.identifier());
        for (const auto &prop: toPrint) {
            const auto value = o.getProperty(prop);
            if (value.isValid()) {
                if (value.canConvert<Sink::ApplicationDomain::Reference>()) {
                    line << compressId(compact, value.toByteArray());
                } else if (value.canConvert<QString>()) {
                    line << value.toString();
                } else if (value.canConvert<QByteArray>()) {
                    line << value.toByteArray();
                } else if (value.canConvert<QByteArrayList>()) {
                    line << value.value<QByteArrayList>().join(", ");
                } else {
                    line << QString("Unprintable type: %1").arg(value.typeName());
                }
            } else {
                line << QString{};
            }
        }
        state.stageTableLine(line);
    }
    state.flushTable();
    state.commandFinished();

    return false;
}

Syntax::List syntax()
{
    Syntax list("list", QObject::tr("List all resources, or the contents of one or more resources."), &SinkList::list, Syntax::EventDriven);
    list.completer = &SinkshUtils::resourceOrTypeCompleter;
    return Syntax::List() << list;
}

REGISTER_SYNTAX(SinkList)

}
