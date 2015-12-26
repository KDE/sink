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

#pragma once

#include <QAbstractItemModel>
#include <QSharedPointer>

#include "common/query.h"
#include "common/clientapi.h"

namespace AkonadishUtils
{

class StoreBase;

bool isValidStoreType(const QString &type);
StoreBase &getStore(const QString &type);
QSharedPointer<QAbstractItemModel> loadModel(const QString &type, Akonadi2::Query query);
QMap<QString, QString> keyValueMapFromArgs(const QStringList &args);

/**
 * A small abstraction layer to use the akonadi store with the type available as string.
 */
class StoreBase {
public:
    virtual Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr getObject() = 0;
    virtual Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr getObject(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier = QByteArray()) = 0;
    virtual KAsync::Job<void> create(const Akonadi2::ApplicationDomain::ApplicationDomainType &type) = 0;
    virtual KAsync::Job<void> modify(const Akonadi2::ApplicationDomain::ApplicationDomainType &type) = 0;
    virtual KAsync::Job<void> remove(const Akonadi2::ApplicationDomain::ApplicationDomainType &type) = 0;
    virtual QSharedPointer<QAbstractItemModel> loadModel(const Akonadi2::Query &query) = 0;
};

template <typename T>
class Store : public StoreBase {
public:
    Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr getObject() Q_DECL_OVERRIDE {
        return T::Ptr::create();
    }

    Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr getObject(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier = QByteArray()) Q_DECL_OVERRIDE {
        return T::Ptr::create(resourceInstanceIdentifier, identifier, 0, QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create());
    }

    KAsync::Job<void> create(const Akonadi2::ApplicationDomain::ApplicationDomainType &type) Q_DECL_OVERRIDE {
        return Akonadi2::Store::create<T>(*static_cast<const T*>(&type));
    }

    KAsync::Job<void> modify(const Akonadi2::ApplicationDomain::ApplicationDomainType &type) Q_DECL_OVERRIDE {
        return Akonadi2::Store::modify<T>(*static_cast<const T*>(&type));
    }

    KAsync::Job<void> remove(const Akonadi2::ApplicationDomain::ApplicationDomainType &type) Q_DECL_OVERRIDE {
        return Akonadi2::Store::remove<T>(*static_cast<const T*>(&type));
    }

    QSharedPointer<QAbstractItemModel> loadModel(const Akonadi2::Query &query) Q_DECL_OVERRIDE {
        return Akonadi2::Store::loadModel<T>(query);
    }
};


}

