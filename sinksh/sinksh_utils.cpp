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

#define REGISTER_TYPE(TYPE) \
        if (type == getTypeName<TYPE>()) { static Store<TYPE> store; return store; } else
SINK_REGISTER_TYPES()
#undef REGISTER_TYPE
        {
            SinkWarning_("", "") << "Trying to get a store that doesn't exist: " << type;
            Q_ASSERT(false);
        }

    static DummyStore store;
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
    } else if (type == getTypeName<Contact>()) {
        return QList<QByteArray>() << Contact::Fn::name << Contact::Emails::name << Contact::Addressbook::name;
    } else if (type == getTypeName<Addressbook>()) {
        return QList<QByteArray>() << Addressbook::Name::name << Addressbook::Parent::name;
    } else if (type == getTypeName<SinkResource>()) {
        return QList<QByteArray>() << SinkResource::ResourceType::name << SinkResource::Account::name << SinkResource::Capabilities::name;
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

bool isId(const QByteArray &value)
{
    return value.startsWith("{");
}

bool applyFilter(Sink::Query &query, const QStringList &args_)
{
    if (args_.isEmpty()) {
        return false;
    }
    auto args = args_;

    auto type = args.takeFirst();

    if ((type.isEmpty() || !SinkshUtils::isValidStoreType(type)) && type != "*") {
        qWarning() << "Unknown type: " << type;
        return false;
    }
    if (type != "*") {
        query.setType(type.toUtf8());
    }
    if (!args.isEmpty()) {
        auto resource = args.takeFirst().toLatin1();

        if (resource.contains('/')) {
            //The resource isn't an id but a path
            auto list = resource.split('/');
            const auto resourceId = list.takeFirst();
            query.resourceFilter(resourceId);
            if (type == Sink::ApplicationDomain::getTypeName<Sink::ApplicationDomain::Mail>() && !list.isEmpty()) {
                auto value = list.takeFirst();
                if (isId(value)) {
                    query.filter<Sink::ApplicationDomain::Mail::Folder>(value);
                } else {
                    Sink::Query folderQuery;
                    folderQuery.resourceFilter(resourceId);
                    folderQuery.filter<Sink::ApplicationDomain::Folder::Name>(value);
                    folderQuery.filter<Sink::ApplicationDomain::Folder::Parent>(QVariant());
                    auto folders = Sink::Store::read<Sink::ApplicationDomain::Folder>(folderQuery);
                    if (folders.size() == 1) {
                        query.filter<Sink::ApplicationDomain::Mail::Folder>(folders.first());
                    } else {
                        qWarning() << "Folder name did not match uniquely: " << folders.size();
                        for (const auto &f : folders) {
                            qWarning() << f.getName();
                        }
                        return false;
                    }
                }
            }
        } else {
            query.resourceFilter(resource);
        }
    }
    return true;
}

bool applyFilter(Sink::Query &query, const SyntaxTree::Options &options)
{
    return applyFilter(query, options.positionalArguments);
}

}
