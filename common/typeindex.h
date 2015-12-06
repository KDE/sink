/*
    Copyright (c) 2015 Christian Mollekopf <mollekopf@kolabsys.com>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/
#pragma once

#include "resultset.h"
#include "bufferadaptor.h"
#include "storage.h"
#include "query.h"
#include <QByteArray>

class TypeIndex
{
public:
    TypeIndex(const QByteArray &type);

    template<typename T>
    void addProperty(const QByteArray &property);

    void add(const QByteArray &identifier, const Akonadi2::ApplicationDomain::BufferAdaptor &bufferAdaptor, Akonadi2::Storage::Transaction &transaction);
    void remove(const QByteArray &identifier, const Akonadi2::ApplicationDomain::BufferAdaptor &bufferAdaptor, Akonadi2::Storage::Transaction &transaction);

    ResultSet query(const Akonadi2::Query &query, QSet<QByteArray> &appliedFilters, Akonadi2::Storage::Transaction &transaction);

private:
    QByteArray mType;
    QByteArrayList mProperties;
    QHash<QByteArray, std::function<void(const QByteArray &identifier, const QVariant &value, Akonadi2::Storage::Transaction &transaction)> > mIndexer;
};

