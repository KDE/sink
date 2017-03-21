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

#include "sinksh_utils.h"
#include "state.h"
#include "syntaxtree.h"

namespace SinkRemove
{

bool remove(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("A type is required"), "sink_remove/02");
        return false;
    }

    if (args.count() < 2) {
        state.printError(QObject::tr("A resource ID is required to remove items"), "sink_remove/03");
        return false;
    }

    if (args.count() < 3) {
        state.printError(QObject::tr("An object ID is required to remove items"), "sink_remove/03");
        return false;
    }

    auto type = args[0];
    auto resourceId = args[1];
    auto identifier = args[2];

    auto &store = SinkshUtils::getStore(type);
    Sink::ApplicationDomain::ApplicationDomainType::Ptr object = store.getObject(resourceId.toUtf8(), identifier.toUtf8());

    auto result = store.remove(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while removing %1 from %1: %2").arg(identifier).arg(resourceId).arg(result.errorMessage()),
                         "akonaid_remove_e" + QString::number(result.errorCode()));
    }

    return true;
}

bool resource(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("A resource can not be removed without an id"), "sink_remove/01");
    }

    auto &store = SinkshUtils::getStore("resource");

    auto resourceId = args.at(0);
    Sink::ApplicationDomain::ApplicationDomainType::Ptr object = store.getObject("", resourceId.toLatin1());

    auto result = store.remove(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while removing the resource %1: %2").arg(resourceId).arg(result.errorMessage()),
                         "akonaid_remove_e" + QString::number(result.errorCode()));
    }

    return true;
}

bool account(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("An account can not be removed without an id"), "sink_remove/01");
    }

    auto &store = SinkshUtils::getStore("account");

    auto id = args.at(0);
    Sink::ApplicationDomain::ApplicationDomainType::Ptr object = store.getObject("", id.toLatin1());

    auto result = store.remove(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while removing the account %1: %2").arg(id).arg(result.errorMessage()),
                         "akonaid_remove_e" + QString::number(result.errorCode()));
    }

    return true;
}

bool identity(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("An identity can not be removed without an id"), "sink_remove/01");
    }

    auto &store = SinkshUtils::getStore("identity");

    auto id = args.at(0);
    Sink::ApplicationDomain::ApplicationDomainType::Ptr object = store.getObject("", id.toLatin1());

    auto result = store.remove(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while removing the identity %1: %2").arg(id).arg(result.errorMessage()),
                         "akonaid_remove_e" + QString::number(result.errorCode()));
    }

    return true;
}


Syntax::List syntax()
{
    Syntax remove("remove", QObject::tr("Remove items in a resource"), &SinkRemove::remove);
    Syntax resource("resource", QObject::tr("Removes a resource"), &SinkRemove::resource, Syntax::NotInteractive);
    Syntax account("account", QObject::tr("Removes a account"), &SinkRemove::account, Syntax::NotInteractive);
    Syntax identity("identity", QObject::tr("Removes an identity"), &SinkRemove::identity, Syntax::NotInteractive);
    resource.completer = &SinkshUtils::resourceCompleter;
    remove.children << resource << account << identity;

    return Syntax::List() << remove;
}

REGISTER_SYNTAX(SinkRemove)

}
