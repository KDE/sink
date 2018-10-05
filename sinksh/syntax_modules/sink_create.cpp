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

using namespace Sink;

namespace SinkCreate
{

Syntax::List syntax();

bool create(const QStringList &allArgs, State &state)
{
    if (allArgs.count() < 2) {
        state.printError(syntax()[0].usage());
        return false;
    }

    auto args = allArgs;
    auto type = args.takeFirst();
    auto &store = SinkshUtils::getStore(type);
    ApplicationDomain::ApplicationDomainType::Ptr object;
    auto resource = SinkshUtils::parseUid(args.takeFirst().toLatin1());
    object = store.getObject(resource);

    auto map = SinkshUtils::keyValueMapFromArgs(args);
    for (auto i = map.begin(); i != map.end(); ++i) {
        const auto property = i.key().toLatin1();
        object->setProperty(property, Sink::PropertyParser::parse(type.toLatin1(), property, i.value()));
    }

    auto result = store.create(*object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while creating the entity: %1").arg(result.errorMessage()),
                         "sink_create_e" + QString::number(result.errorCode()));
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

    const auto resourceType = args.at(0);

    auto map = SinkshUtils::keyValueMapFromArgs(args);

    const auto identifier = SinkshUtils::parseUid(map.take("identifier").toLatin1());

    auto object = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::SinkResource>("", identifier);
    object.setResourceType(resourceType.toLatin1());

    for (auto i = map.begin(); i != map.end(); ++i) {
        //FIXME we need a generic way to convert the value to the right type
        if (i.key() == ApplicationDomain::SinkResource::Account::name) {
            object.setAccount(i.value().toUtf8());
        } else {
            object.setProperty(i.key().toLatin1(), i.value());
        }
    }

    auto result = store.create(object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while creating the entity: %1").arg(result.errorMessage()),
                         "sink_create_e" + QString::number(result.errorCode()));
    }

    return true;
}

bool account(const QStringList &args, State &state)
{
    if (args.isEmpty()) {
        state.printError(QObject::tr("An account can not be created without a type"), "sinkcreate/01");
        return false;
    }

    auto &store = SinkshUtils::getStore("account");

    auto type = args.at(0);

    auto map = SinkshUtils::keyValueMapFromArgs(args);

    auto identifier = map.take("identifier").toLatin1();

    auto object = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::SinkAccount>("", identifier);
    object.setAccountType(type);

    for (auto i = map.begin(); i != map.end(); ++i) {
        object.setProperty(i.key().toLatin1(), i.value());
    }

    auto result = store.create(object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while creating the entity: %1").arg(result.errorMessage()),
                         "sink_create_e" + QString::number(result.errorCode()));
    }

    return true;
}

bool identity(const QStringList &args, State &state)
{
    auto &store = SinkshUtils::getStore("identity");

    auto map = SinkshUtils::keyValueMapFromArgs(args);

    auto identifier = map.take("identifier").toLatin1();

    auto object = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Identity>("", identifier);

    for (auto i = map.begin(); i != map.end(); ++i) {
        //FIXME we need a generic way to convert the value to the right type
        if (i.key() == ApplicationDomain::Identity::Account::name) {
            object.setAccount(i.value().toUtf8());
        } else {
            object.setProperty(i.key().toLatin1(), i.value());
        }
    }

    auto result = store.create(object).exec();
    result.waitForFinished();
    if (result.errorCode()) {
        state.printError(QObject::tr("An error occurred while creating the entity: %1").arg(result.errorMessage()),
                         "sink_create_e" + QString::number(result.errorCode()));
    }

    return true;
}

Syntax::List syntax()
{
    Syntax::List syntax;

    Syntax create("create", QObject::tr("Create items in a resource"), &SinkCreate::create);
    create.addPositionalArgument({"type", "The type of entity to create (mail, event, etc.)"});
    create.addPositionalArgument({"resourceId", "The ID of the resource that will contain the new entity"});
    create.addPositionalArgument({"key value", "Content of the entity", false, true});

    Syntax resource("resource", QObject::tr("Creates a new resource"), &SinkCreate::resource);
    resource.addPositionalArgument({"type", "The type of resource to create" });
    resource.addPositionalArgument({"key value", "Content of the resource", false, true});

    Syntax account("account", QObject::tr("Creates a new account"), &SinkCreate::account);
    account.addPositionalArgument({"type", "The type of account to create" });
    account.addPositionalArgument({"key value", "Content of the account", false, true});

    Syntax identity("identity", QObject::tr("Creates a new identity"), &SinkCreate::identity);
    identity.addPositionalArgument({"key value", "Content of the identity", false, true});

    create.children << resource;
    create.children << account;
    create.children << identity;

    syntax << create;
    return syntax;
}

REGISTER_SYNTAX(SinkCreate)

}
