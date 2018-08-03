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
#include "common/propertyparser.h"

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkModify
{

Syntax::List syntax();

bool modify(const QStringList &args, State &state)
{
    if (args.count() < 3) {
        state.printError(syntax()[0].usage());
        return false;
    }

    auto type = args[0];
    auto resourceId = args[1];
    auto identifier = args[2];

    auto &store = SinkshUtils::getStore(type);
    Sink::ApplicationDomain::ApplicationDomainType::Ptr object = store.getObject(resourceId.toUtf8(), identifier.toUtf8());

    auto map = SinkshUtils::keyValueMapFromArgs(args);
    for (auto i = map.begin(); i != map.end(); ++i) {
        const auto property = i.key().toLatin1();
        object->setProperty(property, Sink::PropertyParser::parse(type.toLatin1(), property, i.value()));
    }

    auto result = store.modify(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while removing %1 from %1: %2").arg(identifier).arg(resourceId).arg(result.errorMessage()),
                         "akonaid__modify_e" + QString::number(result.errorCode()));
    }

    return true;
}

bool resource(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        // TODO: pass the syntax as parameter
        state.printError(QObject::tr("A resource can not be modified without an id"), "sink_modify/01");
    }

    auto &store = SinkshUtils::getStore("resource");

    auto resourceId = args.at(0);
    Sink::ApplicationDomain::ApplicationDomainType::Ptr object = store.getObject("", resourceId.toLatin1());

    auto map = SinkshUtils::keyValueMapFromArgs(args);
    for (auto i = map.begin(); i != map.end(); ++i) {
        const auto property = i.key().toLatin1();
        object->setProperty(property, Sink::PropertyParser::parse("resource", property, i.value()));
    }

    auto result = store.modify(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while modifying the resource %1: %2").arg(resourceId).arg(result.errorMessage()),
                         "akonaid_modify_e" + QString::number(result.errorCode()));
    }

    return true;
}

Syntax::List syntax()
{
    Syntax modify("modify", QObject::tr("Modify items in a resource"), &SinkModify::modify);
    modify.addPositionalArgument({"type", "The type of entity to modify (mail, event, etc.)"});
    modify.addPositionalArgument({"resourceId", "The ID of the resource containing the entity"});
    modify.addPositionalArgument({"objectId", "The ID of the entity"});
    modify.addPositionalArgument({"key value", "Attributes and values to modify", false, true });

    Syntax resource("resource", QObject::tr("Modify a resource"), &SinkModify::resource);//, Syntax::EventDriven);

    resource.addPositionalArgument({"id", "The ID of the resource" });
    resource.addPositionalArgument({"key value", "Attributes and values to modify", false, true});

    resource.completer = &SinkshUtils::resourceOrTypeCompleter;
    modify.children << resource;

    return Syntax::List() << modify;
}

REGISTER_SYNTAX(SinkModify)

}
