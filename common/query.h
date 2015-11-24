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

#include <QByteArrayList>
#include <QHash>
#include <QSet>

namespace Akonadi2 {

/**
 * A query that matches a set of objects
 * 
 * The query will have to be updated regularly similary to the domain objects.
 * It probably also makes sense to have a domain specific part of the query,
 * such as what properties we're interested in (necessary information for on-demand
 * loading of data).
 *
 * The query defines:
 * * what resources to search
 * * filters on various properties (parent collection, startDate range, ....)
 * * properties we need (for on-demand querying)
 * 
 * syncOnDemand: Execute a source sync before executing the query
 * processAll: Ensure all local messages are processed before querying to guarantee an up-to date dataset.
 */
class Query
{
public:
    Query() : syncOnDemand(true), processAll(false), liveQuery(false) {}
    //Could also be a propertyFilter
    QByteArrayList resources;
    //Could also be a propertyFilter
    QByteArrayList ids;
    //Filters to apply
    QHash<QByteArray, QVariant> propertyFilter;
    //Properties to retrieve
    QSet<QByteArray> requestedProperties;
    QByteArray parentProperty;
    bool syncOnDemand;
    bool processAll;
    //If live query is false, this query will not continuously be updated
    bool liveQuery;
};

}
