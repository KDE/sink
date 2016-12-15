/*
 * Copyright (C) 2015 Aaron Seigo <aseigo@kolabsystems.com>
 * Copyright (C) 2015 Christian Mollekopf <mollekopf@kolabsystems.com>
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

#include "sinksh_utils.h"

#include "common/store.h"
#include "common/log.h"

#include "utils.h"

namespace SinkshUtils {

bool isValidStoreType(const QString &type)
{
    return Sink::ApplicationDomain::getTypeNames().contains(type.toLatin1());
}

StoreBase &getStore(const QString &type)
{
    using namespace Sink::ApplicationDomain;

    if (type == getTypeName<Folder>()) {
        static Store<Folder> store;
        return store;
    } else if (type == getTypeName<Mail>()) {
        static Store<Mail> store;
        return store;
    } else if (type == getTypeName<Event>()) {
        static Store<Event> store;
        return store;
    } else if (type == getTypeName<SinkResource>()) {
        static Store<SinkResource> store;
        return store;
    } else if (type == getTypeName<SinkAccount>()) {
        static Store<SinkAccount> store;
        return store;
    } else if (type == getTypeName<Identity>()) {
        static Store<Identity> store;
        return store;
    }

    SinkWarning_("", "") << "Trying to get a store that doesn't exist: " << type;
    Q_ASSERT(false);
    static Store<Sink::ApplicationDomain::ApplicationDomainType> store;
    return store;
}

QList<QByteArray> requestedProperties(const QString &type)
{
    using namespace Sink::ApplicationDomain;
    if (type == getTypeName<Folder>()) {
        return QList<QByteArray>() << Folder::Name::name
                                  << Folder::Parent::name
                                  << Folder::SpecialPurpose::name;
    } else if (type == getTypeName<Mail>()) {
        return QList<QByteArray>() << Mail::Subject::name
                                  << Mail::Folder::name
                                  << Mail::Date::name;
    } else if (type == getTypeName<Event>()) {
        return QList<QByteArray>() << Event::Summary::name;
    } else if (type == getTypeName<SinkResource>()) {
        return QList<QByteArray>() << SinkResource::ResourceType::name << SinkResource::Account::name;
    } else if (type == getTypeName<SinkAccount>()) {
        return QList<QByteArray>() << SinkAccount::AccountType::name << SinkAccount::Name::name;
    } else if (type == getTypeName<Identity>()) {
        return QList<QByteArray>() << Identity::Name::name << Identity::Address::name << Identity::Account::name;
    }
    return QList<QByteArray>();
}

QSharedPointer<QAbstractItemModel> loadModel(const QString &type, Sink::Query query)
{
    query.requestedProperties = requestedProperties(type);
    auto model = getStore(type).loadModel(query);
    Q_ASSERT(model);
    return model;
}

QStringList resourceIds()
{
    Sink::Query query;
    QStringList resources;
    for (const auto &r : getStore("resource").read(query)) {
        resources << r.identifier();
    }
    return resources;
}

QStringList debugareaCompleter(const QStringList &, const QString &fragment, State &state)
{
    return Utils::filteredCompletions(Sink::Log::debugAreas().toList(), fragment);
}

QStringList resourceCompleter(const QStringList &, const QString &fragment, State &state)
{
    return Utils::filteredCompletions(resourceIds(), fragment);
}

static QStringList toStringList(const QByteArrayList &l)
{
    QStringList list;
    for (const auto &s : l) {
        list << s;
    }
    return list;
}

QStringList resourceOrTypeCompleter(const QStringList &commands, const QString &fragment, State &state)
{
    if (commands.count() == 1) {
        return Utils::filteredCompletions(toStringList(Sink::ApplicationDomain::getTypeNames()), fragment);
    }

    return Utils::filteredCompletions(resourceIds(), fragment);
}

QStringList typeCompleter(const QStringList &commands, const QString &fragment, State &state)
{
    return Utils::filteredCompletions(toStringList(Sink::ApplicationDomain::getTypeNames()), fragment);
}

QMap<QString, QString> keyValueMapFromArgs(const QStringList &args)
{
    QMap<QString, QString> map;
    for (int i = 0; i + 2 <= args.size(); i += 2) {
        map.insert(args.at(i), args.at(i + 1));
    }
    return map;
}
}
