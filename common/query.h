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
        /** Leave the query running and continuously update the result set. */
        LiveQuery,
        /** Run the query synchronously. */
        SynchronousQuery
    };
    Q_DECLARE_FLAGS(Flags, Flag)

    struct Comparator {
        enum Comparators {
            Invalid,
            Equals,
            Contains
        };

        Comparator();
        Comparator(const QVariant &v);
        Comparator(const QVariant &v, Comparators c);
        bool matches(const QVariant &v) const;

        QVariant value;
        Comparators comparator;
    };


    static Query PropertyFilter(const QByteArray &key, const QVariant &value)
    {
        Query query;
        query.propertyFilter.insert(key, Comparator(value));
        return query;
    }

    static Query PropertyContainsFilter(const QByteArray &key, const QVariant &value)
    {
        Query query;
        query.propertyFilter.insert(key, Comparator(value, Comparator::Contains));
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

    static Query CapabilityFilter(const QByteArray &capability)
    {
        Query query;
        query.propertyFilter.insert("capabilities", Comparator(capability, Comparator::Contains));
        return query;
    }

    static Query AccountFilter(const QByteArrayList &identifier)
    {
        Q_ASSERT(!identifier.isEmpty());
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
        Q_ASSERT(!identifier.isEmpty());
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
        auto query = IdentityFilter(entity.identifier());
        query.resources << entity.resourceInstanceIdentifier();
        return query;
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

    template <typename T>
    Query &request()
    {
        requestedProperties << T::name;
        return *this;
    }

    template <typename T>
    Query &sort()
    {
        sortProperty = T::name;
        return *this;
    }

    Query(const ApplicationDomain::Entity &value) : limit(0), liveQuery(false), synchronousQuery(false)
    {
        ids << value.identifier();
        resources << value.resourceInstanceIdentifier();
    }

    Query(Flags flags = Flags()) : limit(0), liveQuery(false), synchronousQuery(false)
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
        limit = rhs.limit;
        return *this;
    }

    friend Query operator+(Query lhs, const Query &rhs)
    {
        lhs += rhs;
        return lhs;
    }

    QByteArrayList resources;
    QByteArrayList accounts;
    QByteArrayList ids;
    QHash<QByteArray, Comparator> propertyFilter;
    QByteArrayList requestedProperties;
    QByteArray parentProperty;
    QByteArray sortProperty;
    int limit;
    bool liveQuery;
    bool synchronousQuery;

    class FilterStage {
    public:
        virtual ~FilterStage(){};
    };

    QList<QSharedPointer<FilterStage>> filterStages;

    /*
    * Filters
    */
    class Filter : public FilterStage {
        QByteArrayList ids;
        QHash<QByteArray, Comparator> propertyFilter;
        QByteArray sortProperty;
    };

    template <typename T>
    Query &filter(const QVariant &value)
    {
        return filter(T::name, value);
    }

    template <typename T>
    Query &filter(const Comparator &comparator)
    {
        return filter(T::name, comparator);
    }

    Query &filter(const QByteArray &property, const Comparator &comparator)
    {
        propertyFilter.insert(property, comparator);
        return *this;
    }

    Comparator getFilter(const QByteArray &property) const
    {
        return propertyFilter.value(property);
    }

    bool hasFilter(const QByteArray &property) const
    {
        return propertyFilter.contains(property);
    }

    QHash<QByteArray, Comparator> getBaseFilters() const
    {
        return propertyFilter;
    }

    template <typename T>
    Query &filter(const ApplicationDomain::Entity &value)
    {
        return filter(T::name, QVariant::fromValue(value.identifier()));
    }

    Query &filter(const ApplicationDomain::SinkResource &resource)
    {
        resources << resource.identifier();
        return *this;
    }

    Query &filter(const ApplicationDomain::SinkAccount &account)
    {
        accounts << account.identifier();
        return *this;
    }

    class Reduce : public FilterStage {
    public:

        class Selector {
        public:
            enum Comparator {
                Min, //get the minimum value
                Max, //get the maximum value
                First //Get the first result we get
            };

            template <typename SelectionProperty>
            static Selector max()
            {
                return Selector(SelectionProperty::name, Max);
            }

            Selector(const QByteArray &p, Comparator c)
                : property(p),
                comparator(c)
            {
            }

            QByteArray property;
            Comparator comparator;
        };

        Reduce(const QByteArray &p, const Selector &s)
            : property(p),
            selector(s)
        {
        }

        //Reduce on property
        QByteArray property;
        Selector selector;

        //TODO add aggregate functions like:
        //.count()
        //.collect<Mail::sender>();
        //...
        //
        //Potentially pass-in an identifier under which the result will be available in the result set.
    };

    template <typename T>
    Reduce &reduce(const Reduce::Selector &s)
    {
        auto reduction = QSharedPointer<Reduce>::create(T::name, s);
        filterStages << reduction;
        return *reduction;
    }

    /**
    * "Bloom" on a property.
    *
    * For every encountered value of a property,
    * a result set is generated containing all entries with the same value.
    *
    * Example:
    * For an input result set of one mail; return all emails with the same threadId.
    */
    class Bloom : public FilterStage {
    public:
        //Property to bloom on
        QByteArray property;
        Bloom(const QByteArray &p)
            : property(p)
        {
        }
    };

    template <typename T>
    void bloom()
    {
        auto bloom = QSharedPointer<Bloom>::create(T::name);
        filterStages << bloom;
    }

};

}

QDebug operator<<(QDebug dbg, const Sink::Query::Comparator &c);

Q_DECLARE_OPERATORS_FOR_FLAGS(Sink::Query::Flags)
