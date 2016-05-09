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

using namespace Sink;

QDebug operator<<(QDebug dbg, const Sink::Query::Comparator &c)
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

Query::Comparator::Comparator() : comparator(Invalid)
{
}

Query::Comparator::Comparator(const QVariant &v) : value(v), comparator(Equals)
{
}

Query::Comparator::Comparator(const QVariant &v, Comparators c) : value(v), comparator(c)
{
}

bool Query::Comparator::matches(const QVariant &v) const
{
    switch(comparator) {
        case Equals:
            return v == value;
        case Contains:
            return v.value<QByteArrayList>().contains(value.toByteArray());
        default:
            break;
    }
    return false;
}
