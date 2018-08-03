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

Syntax::List syntax();

bool remove(const QStringList &args, State &state)
{
    if (args.count() < 3) {
        state.printError(syntax()[0].usage());
        return false;
    }

    auto type = args[0];
    const auto resourceId = SinkshUtils::parseUid(args.at(1).toUtf8());
    const auto identifier = SinkshUtils::parseUid(args.at(2).toUtf8());

    auto &store = SinkshUtils::getStore(type);
    Sink::ApplicationDomain::ApplicationDomainType::Ptr object = store.getObject(resourceId, identifier);

    auto result = store.remove(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while removing %1 from %1: %2").arg(QString{identifier}).arg(QString{resourceId}).arg(result.errorMessage()),
                         "akonaid_remove_e" + QString::number(result.errorCode()));
    }

    return true;
}

bool resource(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("A resource can not be removed without an id"), "sink_remove/01");
        return false;
    }

    auto &store = SinkshUtils::getStore("resource");

    const auto resourceId = SinkshUtils::parseUid(args.at(0).toUtf8());
    Sink::ApplicationDomain::ApplicationDomainType::Ptr object = store.getObject("", resourceId);

    auto result = store.remove(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while removing the resource %1: %2").arg(QString{resourceId}).arg(result.errorMessage()),
                         "akonaid_remove_e" + QString::number(result.errorCode()));
    }

    return true;
}

bool account(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("An account can not be removed without an id"), "sink_remove/01");
        return false;
    }

    auto &store = SinkshUtils::getStore("account");

    const auto id = SinkshUtils::parseUid(args.at(0).toUtf8());
    Sink::ApplicationDomain::ApplicationDomainType::Ptr object = store.getObject("", id);

    auto result = store.remove(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while removing the account %1: %2").arg(QString{id}).arg(result.errorMessage()),
                         "akonaid_remove_e" + QString::number(result.errorCode()));
    }

    return true;
}

bool identity(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("An identity can not be removed without an id"), "sink_remove/01");
        return false;
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

    remove.addPositionalArgument({ .name = "type", .help = "The type of entity to remove (mail, event, etc.)" });
    remove.addPositionalArgument({ .name = "resourceId", .help = "The ID of the resource containing the entity" });
    remove.addPositionalArgument({ .name = "objectId", .help = "The ID of the entity to remove" });

    Syntax resource("resource", QObject::tr("Removes a resource"), &SinkRemove::resource, Syntax::NotInteractive);
    resource.addPositionalArgument({ .name = "id", .help = "The ID of the resource to remove" });
    resource.completer = &SinkshUtils::resourceCompleter;

    Syntax account("account", QObject::tr("Removes a account"), &SinkRemove::account, Syntax::NotInteractive);
    account.addPositionalArgument({ .name = "id", .help = "The ID of the account to remove" });

    Syntax identity("identity", QObject::tr("Removes an identity"), &SinkRemove::identity, Syntax::NotInteractive);
    identity.addPositionalArgument({ .name = "id", .help = "The ID of the account to remove" });

    remove.children << resource << account << identity;

    return Syntax::List() << remove;
}

REGISTER_SYNTAX(SinkRemove)

}
