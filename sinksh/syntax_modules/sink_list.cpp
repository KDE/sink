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
#include "common/store.h"
#include "common/propertyparser.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkList
{

static QByteArray compressId(bool compress, const QByteArray &id)
{
    if (!compress) {
        if (id.startsWith('{')) {
            return id.mid(1, id.length() - 2);
        }
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

QStringList printToList(const Sink::ApplicationDomain::ApplicationDomainType &o, bool compact, const QByteArrayList &toPrint)
{
    QStringList line;
    line << compressId(compact, o.resourceInstanceIdentifier());
    line << compressId(compact, o.identifier());
    for (const auto &prop: toPrint) {
        const auto value = o.getProperty(prop);
        if (value.isValid()) {
            if (value.canConvert<Sink::ApplicationDomain::Reference>()) {
                line << compressId(compact, value.toByteArray());
            } else if (value.canConvert<Sink::ApplicationDomain::BLOB>()) {
                line << value.value<Sink::ApplicationDomain::BLOB>().value;
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
    return line;
}

bool list(const QStringList &args_, State &state)
{
    if (args_.isEmpty()) {
        state.printError(QObject::tr("Options: $type [--resource $resource] [--compact] [--filter $property=$value] [--id $id] [--showall|--show $property]"));
        return false;
    }

    auto options = SyntaxTree::parseOptions(args_);
    bool asLine = true;

    Sink::Query query;
    query.setId("list");
    if (!SinkshUtils::applyFilter(query, options)) {
        state.printError(QObject::tr("Options: $type [--resource $resource] [--compact] [--filter $property=$value] [--showall|--show $property]"));
        return false;
    }

    auto compact = options.options.contains("compact");
    if (!options.options.contains("showall")) {
        if (options.options.contains("show")) {
            auto list = options.options.value("show");
            std::transform(list.constBegin(), list.constEnd(), std::back_inserter(query.requestedProperties), [] (const QString &s) { return s.toLatin1(); });
        } else {
            query.requestedProperties = SinkshUtils::requestedProperties(query.type());
        }
    } else {
        asLine = false;
    }

    QByteArrayList toPrint;
    QStringList tableLine;

    for (const auto &o : SinkshUtils::getStore(query.type()).read(query)) {
        if (tableLine.isEmpty()) {
            tableLine << QObject::tr("Resource") << QObject::tr("Identifier");
            if (query.requestedProperties.isEmpty()) {
                toPrint = o.availableProperties();
                std::sort(toPrint.begin(), toPrint.end());
            } else {
                toPrint = query.requestedProperties;
                std::sort(toPrint.begin(), toPrint.end());
            }
            if (asLine) {
                auto in = toPrint;
                std::transform(in.constBegin(), in.constEnd(), std::back_inserter(tableLine), [] (const QByteArray &s) -> QString { return s; });
                state.stageTableLine(tableLine);
            }
        }
        if (asLine) {
            state.stageTableLine(printToList(o, compact, toPrint));
        } else {
            state.stageTableLine(QStringList());
            auto list = printToList(o, compact, toPrint);
            state.stageTableLine(QStringList() << "Resource: " << list.value(0));
            state.stageTableLine(QStringList() << "Identifier: " << list.value(1));
            for (int i = 0; i < (list.size() - 2); i++) {
                state.stageTableLine(QStringList() << toPrint.value(i) << list.value(i + 2));
            }
            state.flushTable();
        }
    }
    state.flushTable();
    return true;
}

Syntax::List syntax()
{
    Syntax list("list", QObject::tr("List all resources, or the contents of one or more resources."), &SinkList::list, Syntax::NotInteractive);
    list.completer = &SinkshUtils::resourceOrTypeCompleter;
    return Syntax::List() << list;
}

REGISTER_SYNTAX(SinkList)

}
