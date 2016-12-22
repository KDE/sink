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
#include "query.h"

#include <QList>
#include <QDataStream>

using namespace Sink;

static const int registerQuery = qRegisterMetaTypeStreamOperators<Sink::QueryBase>();

QDebug operator<<(QDebug dbg, const Sink::QueryBase::Comparator &c)
{
    if (c.comparator == Sink::Query::Comparator::Equals) {
        dbg.nospace() << "== " << c.value;
    } else if (c.comparator == Sink::Query::Comparator::Contains) {
        dbg.nospace() << "contains " << c.value;
    } else {
        dbg.nospace() << "unknown comparator: " << c.value;
    }

    return dbg.space();
}

QDebug operator<<(QDebug dbg, const Sink::QueryBase::Filter &filter)
{
    if (filter.ids.isEmpty()) {
        dbg.nospace() << "Filter(" << filter.propertyFilter << ")";
    } else {
        dbg.nospace() << "Filter(" << filter.ids << ")";
    }
    return dbg.maybeSpace();
}

QDebug operator<<(QDebug dbg, const Sink::QueryBase &query)
{
    dbg.nospace() << "Query [" << query.type() << "] << Id: " << query.id() << "\n";
    dbg.nospace() << "  Filter: " << query.getBaseFilters() << "\n";
    dbg.nospace() << "  Ids: " << query.ids() << "\n";
    dbg.nospace() << "  Sorting: " << query.sortProperty() << "\n";
    return dbg.maybeSpace();
}

QDebug operator<<(QDebug dbg, const Sink::Query &query)
{
    dbg << static_cast<Sink::QueryBase>(query);
    dbg.nospace() << "  Requested: " << query.requestedProperties << "\n";
    dbg.nospace() << "  Parent: " << query.parentProperty() << "\n";
    dbg.nospace() << "  IsLive: " << query.liveQuery() << "\n";
    dbg.nospace() << "  ResourceFilter: " << query.getResourceFilter() << "\n";
    return dbg.maybeSpace();
}

QDataStream & operator<< (QDataStream &stream, const Sink::QueryBase::Comparator &comparator)
{
    stream << comparator.comparator;
    stream << comparator.value;
    return stream;
}

QDataStream & operator>> (QDataStream &stream, Sink::QueryBase::Comparator &comparator)
{
    int c;
    stream >> c;
    comparator.comparator = static_cast<Sink::QueryBase::Comparator::Comparators>(c);
    stream >> comparator.value;
    return stream;
}

QDataStream & operator<< (QDataStream &stream, const Sink::QueryBase::Filter &filter)
{
    stream << filter.ids;
    stream << filter.propertyFilter;
    return stream;
}

QDataStream & operator>> (QDataStream &stream, Sink::QueryBase::Filter &filter)
{
    stream >> filter.ids;
    stream >> filter.propertyFilter;
    return stream;
}

QDataStream & operator<< (QDataStream &stream, const Sink::QueryBase &query)
{
    stream << query.type();
    stream << query.sortProperty();
    stream << query.getFilter();
    return stream;
}

QDataStream & operator>> (QDataStream &stream, Sink::QueryBase &query)
{
    QByteArray type;
    stream >> type;
    query.setType(type);
    QByteArray sortProperty;
    stream >> sortProperty;
    query.setSortProperty(sortProperty);
    Sink::QueryBase::Filter filter;
    stream >> filter;
    query.setFilter(filter);
    return stream;
}

bool QueryBase::Filter::operator==(const QueryBase::Filter &other) const
{
    auto ret = ids == other.ids && propertyFilter == other.propertyFilter;
    return ret;
}

bool QueryBase::operator==(const QueryBase &other) const
{
    auto ret = mType == other.mType 
        && mSortProperty == other.mSortProperty 
        && mBaseFilterStage == other.mBaseFilterStage;
    return ret;
}

QueryBase::Comparator::Comparator() : comparator(Invalid)
{
}

QueryBase::Comparator::Comparator(const QVariant &v) : value(v), comparator(Equals)
{
}

QueryBase::Comparator::Comparator(const QVariant &v, Comparators c) : value(v), comparator(c)
{
}

bool QueryBase::Comparator::matches(const QVariant &v) const
{
    switch(comparator) {
        case Equals:
            if (!v.isValid()) {
                if (!value.isValid()) {
                    return true;
                }
                return false;
            }
            return v == value;
        case Contains:
            if (!v.isValid()) {
                return false;
            }
            return v.value<QByteArrayList>().contains(value.toByteArray());
        case In:
            if (!v.isValid()) {
                return false;
            }
            return value.value<QByteArrayList>().contains(v.toByteArray());
        case Invalid:
        default:
            break;
    }
    return false;
}

bool Query::Comparator::operator==(const Query::Comparator &other) const
{
    return value == other.value && comparator == other.comparator;
}
