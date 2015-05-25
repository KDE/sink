/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include "event.h"

#include <QVector>
#include <QByteArray>

#include "../resultset.h"
#include "../index.h"
#include "../storage.h"
#include "../log.h"

using namespace Akonadi2::ApplicationDomain;

ResultSet EventImplementation::queryIndexes(const Akonadi2::Query &query, const QByteArray &resourceInstanceIdentifier)
{
    QVector<QByteArray> keys;
    if (query.propertyFilter.contains("uid")) {
        Index uidIndex(Akonadi2::Store::storageLocation(), resourceInstanceIdentifier + "index.uid", Akonadi2::Storage::ReadOnly);
        uidIndex.lookup(query.propertyFilter.value("uid").toByteArray(), [&](const QByteArray &value) {
            keys << value;
        },
        [](const Index::Error &error) {
            Warning() << "Error in index: " <<  error.message;
        });
    }
    return ResultSet(keys);
}

void EventImplementation::index(const Event &type)
{
    Index uidIndex(Akonadi2::Store::storageLocation(), type.resourceInstanceIdentifier() + "index.uid", Akonadi2::Storage::ReadWrite);
    const auto uid = type.getProperty("uid");
    if (uid.isValid()) {
        uidIndex.add(uid.toByteArray(), type.identifier());
    }
}
