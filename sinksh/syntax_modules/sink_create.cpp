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

namespace SinkCreate
{

bool create(const QStringList &allArgs, State &state)
{
    if (allArgs.isEmpty()) {
        state.printError(QObject::tr("A type is required"), "sinkcreate/02");
        return false;
    }

    if (allArgs.count() < 2) {
        state.printError(QObject::tr("A resource ID is required to create items"), "sinkcreate/03");
        return false;
    }

    auto args = allArgs;
    auto type = args.takeFirst();
    auto &store = SinkshUtils::getStore(type);
    Sink::ApplicationDomain::ApplicationDomainType::Ptr object;
    auto resource = args.takeFirst().toLatin1();
    object = store.getObject(resource);

    auto map = SinkshUtils::keyValueMapFromArgs(args);
    for (auto i = map.begin(); i != map.end(); ++i) {
        object->setProperty(i.key().toLatin1(), i.value());
    }

    auto result = store.create(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while creating the entity: %1").arg(result.errorMessage()),
                         "akonaid_create_e" + QString::number(result.errorCode()));
    }

    return true;
}

bool resource(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("A resource can not be created without a type"), "sinkcreate/01");
        return false;
    }

    auto &store = SinkshUtils::getStore("resource");

    auto resourceType = args.at(0);
    Sink::ApplicationDomain::ApplicationDomainType::Ptr object = store.getObject("");
    object->setProperty("type", resourceType);

    auto map = SinkshUtils::keyValueMapFromArgs(args);
    for (auto i = map.begin(); i != map.end(); ++i) {
        object->setProperty(i.key().toLatin1(), i.value());
    }

    auto result = store.create(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while creating the entity: %1").arg(result.errorMessage()),
                         "akonaid_create_e" + QString::number(result.errorCode()));
    }

    return true;
}


Syntax::List syntax()
{
    Syntax::List syntax;

    Syntax create("create", QObject::tr("Create items in a resource"), &SinkCreate::create);
    create.children << Syntax("resource", QObject::tr("Creates a new resource"), &SinkCreate::resource);

    syntax << create;
    return syntax;
}

REGISTER_SYNTAX(SinkCreate)

}
