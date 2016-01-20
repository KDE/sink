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

#include "state.h"

namespace SinkshUtils
{

class StoreBase;

bool isValidStoreType(const QString &type);
StoreBase &getStore(const QString &type);
QSharedPointer<QAbstractItemModel> loadModel(const QString &type, Sink::Query query);
QStringList resourceIds();
QStringList resourceCompleter(const QStringList &, const QString &fragment, State &state);
QStringList resourceOrTypeCompleter(const QStringList &commands, const QString &fragment, State &state);
QStringList typeCompleter(const QStringList &commands, const QString &fragment, State &state);
QMap<QString, QString> keyValueMapFromArgs(const QStringList &args);

/**
 * A small abstraction layer to use the sink store with the type available as string.
 */
class StoreBase {
public:
    virtual Sink::ApplicationDomain::ApplicationDomainType::Ptr getObject() = 0;
    virtual Sink::ApplicationDomain::ApplicationDomainType::Ptr getObject(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier = QByteArray()) = 0;
    virtual KAsync::Job<void> create(const Sink::ApplicationDomain::ApplicationDomainType &type) = 0;
    virtual KAsync::Job<void> modify(const Sink::ApplicationDomain::ApplicationDomainType &type) = 0;
    virtual KAsync::Job<void> remove(const Sink::ApplicationDomain::ApplicationDomainType &type) = 0;
    virtual QSharedPointer<QAbstractItemModel> loadModel(const Sink::Query &query) = 0;
};

template <typename T>
class Store : public StoreBase {
public:
    Sink::ApplicationDomain::ApplicationDomainType::Ptr getObject() Q_DECL_OVERRIDE {
        return T::Ptr::create();
    }

    Sink::ApplicationDomain::ApplicationDomainType::Ptr getObject(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier = QByteArray()) Q_DECL_OVERRIDE {
        return T::Ptr::create(resourceInstanceIdentifier, identifier, 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
    }

    KAsync::Job<void> create(const Sink::ApplicationDomain::ApplicationDomainType &type) Q_DECL_OVERRIDE {
        return Sink::Store::create<T>(*static_cast<const T*>(&type));
    }

    KAsync::Job<void> modify(const Sink::ApplicationDomain::ApplicationDomainType &type) Q_DECL_OVERRIDE {
        return Sink::Store::modify<T>(*static_cast<const T*>(&type));
    }

    KAsync::Job<void> remove(const Sink::ApplicationDomain::ApplicationDomainType &type) Q_DECL_OVERRIDE {
        return Sink::Store::remove<T>(*static_cast<const T*>(&type));
    }

    QSharedPointer<QAbstractItemModel> loadModel(const Sink::Query &query) Q_DECL_OVERRIDE {
        return Sink::Store::loadModel<T>(query);
    }
};


}

