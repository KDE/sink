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

#include "akonadish_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace AkonadiRemove
{

bool remove(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("A type is required"), "akonadicreate/02");
        return false;
    }

    if (args.count() < 2) {
        state.printError(QObject::tr("A resource ID is required to remove items"), "akonadicreate/03");
        return false;
    }

    if (args.count() < 3) {
        state.printError(QObject::tr("An object ID is required to remove items"), "akonadicreate/03");
        return false;
    }

    auto type = args[0];
    auto resourceId = args[1];
    auto identifier = args[2];

    auto &store = AkonadishUtils::getStore(type);
    Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr object = store.getObject(resourceId.toUtf8(), identifier.toUtf8());

    auto result = store.remove(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while removing %1 from %1: %2").arg(identifier).arg(resourceId).arg(result.errorMessage()),
                         "akonaid_create_" + QString::number(result.errorCode()));
    }

    return true;
}

bool resource(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("A resource can not be removed without an id"), "akonadi_remove/01");
    }

    auto &store = AkonadishUtils::getStore("resource");

    auto resourceId = args.at(0);
    Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr object = store.getObject("", resourceId.toLatin1());

    auto result = store.remove(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while removing the resource %1: %2").arg(resourceId).arg(result.errorMessage()),
                         "akonaid_create_" + QString::number(result.errorCode()));
    }

    return true;
}


Syntax::List syntax()
{
    Syntax::List syntax;

    Syntax create("remove");
    create.children << Syntax("resource", QObject::tr("Removes a resource"), &AkonadiRemove::resource);//, Syntax::EventDriven);

    syntax << create;
    return syntax;
}

REGISTER_SYNTAX(AkonadiRemove)

}
