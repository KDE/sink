/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#pragma once

#include "sink_export.h"
#include <QByteArrayList>
#include <QHash>
#include <QSet>
#include "applicationdomaintype.h"

namespace Sink {

/**
 * A query that matches a set of entities.
 */
class SINK_EXPORT Query
{
public:
    enum Flag
    {
        /** Leave the query running an contiously update the result set. */
        LiveQuery
    };
    Q_DECLARE_FLAGS(Flags, Flag)

    static Query PropertyFilter(const QByteArray &key, const QVariant &value)
    {
        Query query;
        query.propertyFilter.insert(key, value);
        return query;
    }

    static Query PropertyContainsFilter(const QByteArray &key, const QVariant &value)
    {
        Query query;
        query.propertyFilter.insert(key, value);
        return query;
    }

    static Query PropertyFilter(const QByteArray &key, const ApplicationDomain::Entity &entity)
    {
        return PropertyFilter(key, QVariant::fromValue(entity.identifier()));
    }

    static Query ResourceFilter(const QByteArray &identifier)
    {
        Query query;
        query.resources.append(identifier);
        return query;
    }

    static Query ResourceFilter(const QByteArrayList &identifier)
    {
        Query query;
        query.resources = identifier;
        return query;
    }

    static Query ResourceFilter(const ApplicationDomain::SinkResource &entity)
    {
        return ResourceFilter(entity.identifier());
    }

    static Query AccountFilter(const QByteArray &identifier)
    {
        Query query;
        query.accounts.append(identifier);
        return query;
    }

    static Query AccountFilter(const QByteArrayList &identifier)
    {
        Query query;
        query.accounts = identifier;
        return query;
    }

    static Query AccountFilter(const ApplicationDomain::SinkAccount &entity)
    {
        return AccountFilter(entity.identifier());
    }

    static Query IdentityFilter(const QByteArray &identifier)
    {
        Query query;
        query.ids << identifier;
        return query;
    }

    static Query IdentityFilter(const QByteArrayList &identifier)
    {
        Query query;
        query.ids = identifier;
        return query;
    }

    static Query IdentityFilter(const ApplicationDomain::Entity &entity)
    {
        return IdentityFilter(entity.identifier());
    }

    static Query RequestedProperties(const QByteArrayList &properties)
    {
        Query query;
        query.requestedProperties = properties;
        return query;
    }

    static Query RequestTree(const QByteArray &parentProperty)
    {
        Query query;
        query.parentProperty = parentProperty;
        return query;
    }

    Query(Flags flags = Flags()) : limit(0)
    {
    }

    Query &operator+=(const Query &rhs)
    {
        resources += rhs.resources;
        accounts += rhs.accounts;
        ids += rhs.ids;
        for (auto it = rhs.propertyFilter.constBegin(); it != rhs.propertyFilter.constEnd(); it++) {
            propertyFilter.insert(it.key(), it.value());
        }
        requestedProperties += rhs.requestedProperties;
        parentProperty = rhs.parentProperty;
        sortProperty = rhs.sortProperty;
        liveQuery = rhs.liveQuery;
        limit = rhs.limit;
        return *this;
    }

    friend Query operator+(Query lhs, const Query &rhs)
    {
        lhs += rhs;
        return lhs;
    }

    struct Comparator {
        enum Comparators {
            Invalid,
            Equals,
            Contains
        };

        Comparator();
        Comparator(const QVariant &v);
        bool matches(const QVariant &v) const;

        QVariant value;
        Comparators comparator;
    };

    QByteArrayList resources;
    QByteArrayList accounts;
    QByteArrayList ids;
    QHash<QByteArray, Comparator> propertyFilter;
    QByteArrayList requestedProperties;
    QByteArray parentProperty;
    QByteArray sortProperty;
    bool liveQuery;
    int limit;
};
}

QDebug operator<<(QDebug dbg, const Sink::Query::Comparator &c);

Q_DECLARE_OPERATORS_FOR_FLAGS(Sink::Query::Flags)
