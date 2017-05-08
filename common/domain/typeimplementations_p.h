/*
 *   Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "typeindex.h"
#include <QMap>

template <typename T, typename First>
void mergeImpl(T &map, First f)
{
    for (auto it = f.constBegin(); it != f.constEnd(); it++) {
        map.insert(it.key(), it.value());
    }
}

template <typename T, typename First, typename ... Tail>
void mergeImpl(T &map, First f, Tail ...maps)
{
    for (auto it = f.constBegin(); it != f.constEnd(); it++) {
        map.insert(it.key(), it.value());
    }
    mergeImpl<T, Tail...>(map, maps...);
}

template <typename First, typename ... Tail>
First merge(First f, Tail ...maps)
{
    First map;
    mergeImpl(f, maps...);
    return map;
}

template <typename Property>
class ValueIndex
{
public:
    static void configure(TypeIndex &index)
    {
        index.addProperty<Property>();
    }

    template <typename EntityType>
    static QMap<QByteArray, int> databases()
    {
        return {{QByteArray{EntityType::name} +".index." + Property::name, 1}};
    }
};


template <typename Property, typename SortProperty>
class SortedIndex
{
public:
    static void configure(TypeIndex &index)
    {
        index.addPropertyWithSorting<Property, SortProperty>();
    }

    template <typename EntityType>
    static QMap<QByteArray, int> databases()
    {
        return {{QByteArray{EntityType::name} +".index." + Property::name + ".sort." + SortProperty::name, 1}};
    }
};

template <typename Property, typename SecondaryProperty>
class SecondaryIndex
{
public:
    static void configure(TypeIndex &index)
    {
        index.addSecondaryProperty<Property, SecondaryProperty>();
    }

    template <typename EntityType>
    static QMap<QByteArray, int> databases()
    {
        return {{QByteArray{EntityType::name} +".index." + Property::name + SecondaryProperty::name, 1}};
    }
};

template <typename Property, typename SecondaryProperty, typename Indexer>
class CustomSecondaryIndex
{
public:
    static void configure(TypeIndex &index)
    {
        index.addSecondaryPropertyIndexer<Property, SecondaryProperty, Indexer>();
    }

    template <typename EntityType>
    static QMap<QByteArray, int> databases()
    {
        return Indexer::databases();
    }
};

template <typename EntityType, typename ... Indexes>
class IndexConfig
{
    template <typename T>
    static void applyIndex(TypeIndex &index)
    {
        T::configure(index);
    }

    ///Apply recursively for parameter pack
    template <typename First, typename Second, typename ... Tail>
    static void applyIndex(TypeIndex &index)
    {
        applyIndex<First>(index);
        applyIndex<Second, Tail...>(index);
    }

    template <typename T>
    static QMap<QByteArray, int> getDbs()
    {
        return T::template databases<EntityType>();
    }

    template <typename First, typename Second, typename ... Tail>
    static QMap<QByteArray, int> getDbs()
    {
        return merge(getDbs<First>(), getDbs<Second, Tail...>());
    }

public:
    static void configure(TypeIndex &index)
    {
        applyIndex<Indexes...>(index);
    }

    static QMap<QByteArray, int> databases()
    {
        return getDbs<Indexes...>();
    }

};

