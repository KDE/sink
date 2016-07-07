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

static QStringList s_types = QStringList() << "resource"
                                           << "folder"
                                           << "mail"
                                           << "event"
                                           << "account";

bool isValidStoreType(const QString &type)
{
    return s_types.contains(type);
}

StoreBase &getStore(const QString &type)
{
    if (type == "folder") {
        static Store<Sink::ApplicationDomain::Folder> store;
        return store;
    } else if (type == "mail") {
        static Store<Sink::ApplicationDomain::Mail> store;
        return store;
    } else if (type == "event") {
        static Store<Sink::ApplicationDomain::Event> store;
        return store;
    } else if (type == "resource") {
        static Store<Sink::ApplicationDomain::SinkResource> store;
        return store;
    } else if (type == "account") {
        static Store<Sink::ApplicationDomain::SinkAccount> store;
        return store;
    }

    // TODO: reinstate the warning+assert
    // Q_ASSERT(false);
    // qWarning() << "Trying to get a store that doesn't exist, falling back to event";
    static Store<Sink::ApplicationDomain::Event> store;
    return store;
}

QList<QByteArray> requestedProperties(const QString &type)
{
    if (type == "folder") {
        return QList<QByteArray>() << "name"
                                  << "parent";
    } else if (type == "mail") {
        return QList<QByteArray>() << "subject"
                                  << "folder"
                                  << "date";
    } else if (type == "event") {
        return QList<QByteArray>() << "summary";
    } else if (type == "resource") {
        return QList<QByteArray>() << "type";
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

QStringList resourceIds(State &state)
{
    QStringList resources;
    Sink::Query query;
    query.liveQuery = false;
    auto model = SinkshUtils::loadModel("resource", query);

    QObject::connect(model.data(), &QAbstractItemModel::rowsInserted, [model, &resources](const QModelIndex &index, int start, int end) mutable {
        for (int i = start; i <= end; i++) {
            auto object = model->data(model->index(i, 0, index), Sink::Store::DomainObjectBaseRole).value<Sink::ApplicationDomain::ApplicationDomainType::Ptr>();
            resources << object->identifier();
        }
    });

    QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [model, state](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
        if (roles.contains(Sink::Store::ChildrenFetchedRole)) {
            state.commandFinished();
        }
    });

    state.commandStarted();

    return resources;
}

QStringList debugareaCompleter(const QStringList &, const QString &fragment, State &state)
{
    return Utils::filteredCompletions(Sink::Log::debugAreas().toList(), fragment);
}

QStringList resourceCompleter(const QStringList &, const QString &fragment, State &state)
{
    return Utils::filteredCompletions(resourceIds(state), fragment);
}

QStringList resourceOrTypeCompleter(const QStringList &commands, const QString &fragment, State &state)
{
    static QStringList types = s_types;
    if (commands.count() == 1) {
        return Utils::filteredCompletions(s_types, fragment);
    }

    return Utils::filteredCompletions(resourceIds(state), fragment);
}

QStringList typeCompleter(const QStringList &commands, const QString &fragment, State &state)
{
    return Utils::filteredCompletions(s_types, fragment);
}

QMap<QString, QString> keyValueMapFromArgs(const QStringList &args)
{
    // TODO: this is not the most clever of algorithms. preserved during the port of commands
    // from sink_client ... we can probably do better, however ;)
    QMap<QString, QString> map;
    for (int i = 0; i + 2 <= args.size(); i += 2) {
        map.insert(args.at(i), args.at(i + 1));
    }

    return map;
}
}
