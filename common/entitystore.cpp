/*
 * Copyright (C) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "entitystore.h"

using namespace Sink;

EntityStore::EntityStore(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier, Sink::Storage::Transaction &transaction)
    : mResourceType(resourceType), mResourceInstanceIdentifier(resourceInstanceIdentifier),
    mTransaction(transaction)
{

}

QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> EntityStore::getLatest(const Sink::Storage::NamedDatabase &db, const QByteArray &uid, DomainTypeAdaptorFactoryInterface &adaptorFactory)
{
    QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> current;
    db.findLatest(uid,
        [&current, &adaptorFactory](const QByteArray &key, const QByteArray &data) -> bool {
            Sink::EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
            if (!buffer.isValid()) {
                Warning() << "Read invalid buffer from disk";
            } else {
                Trace() << "Found value " << key;
                current = adaptorFactory.createAdaptor(buffer.entity());
            }
            return false;
        },
        [](const Sink::Storage::Error &error) { Warning() << "Failed to read current value from storage: " << error.message; });
    return current;
}

