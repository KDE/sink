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

#include "akonadish_utils.h"

#include "common/clientapi.h"

namespace AkonadishUtils
{

bool isValidStoreType(const QString &type)
{
    static const QSet<QString> types = QSet<QString>() << "folder" << "mail" << "event" << "resource";
    return types.contains(type);
}

StoreBase &getStore(const QString &type)
{
    if (type == "folder") {
        static Store<Akonadi2::ApplicationDomain::Folder> store;
        return store;
    } else if (type == "mail") {
        static Store<Akonadi2::ApplicationDomain::Mail> store;
        return store;
    } else if (type == "event") {
        static Store<Akonadi2::ApplicationDomain::Event> store;
        return store;
    } else if (type == "resource") {
        static Store<Akonadi2::ApplicationDomain::AkonadiResource> store;
        return store;
    }

    //TODO: reinstate the warning+assert
    //Q_ASSERT(false);
    //qWarning() << "Trying to get a store that doesn't exist, falling back to event";
    static Store<Akonadi2::ApplicationDomain::Event> store;
    return store;
}

QSharedPointer<QAbstractItemModel> loadModel(const QString &type, Akonadi2::Query query)
{
    if (type == "folder") {
        query.requestedProperties << "name" << "parent";
    } else if (type == "mail") {
        query.requestedProperties << "subject" << "folder" << "date";
    } else if (type == "event") {
        query.requestedProperties << "summary";
    } else if (type == "resource") {
        query.requestedProperties << "type";
    }
    auto model = getStore(type).loadModel(query);
    Q_ASSERT(model);
    return model;
}

QMap<QString, QString> keyValueMapFromArgs(const QStringList &args)
{
    //TODO: this is not the most clever of algorithms. preserved during the port of commands
    // from akonadi2_client ... we can probably do better, however ;)
    QMap<QString, QString> map;
    for (int i = 0; i + 2 <= args.size(); i += 2) {
        map.insert(args.at(i), args.at(i + 1));
    }

    return map;
}

}

