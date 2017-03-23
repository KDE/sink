/*
 * Copyright (C) 2017 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include <QHash>
#include <QString>
#include <QVariant>
#include <functional>

#include "applicationdomaintype.h"

namespace Sink {
namespace Private {

template <typename T>
QVariant parseString(const QString &);

template <>
QVariant parseString<QString>(const QString &s);

template <>
QVariant parseString<QByteArray>(const QString &s);

template <>
QVariant parseString<Sink::ApplicationDomain::Reference>(const QString &s);

template <>
QVariant parseString<Sink::ApplicationDomain::BLOB>(const QString &s);

template <>
QVariant parseString<bool>(const QString &s);

template <>
QVariant parseString<int>(const QString &s);

template <>
QVariant parseString<QList<QByteArray>>(const QString &s);

template <>
QVariant parseString<QDateTime>(const QString &s);

template <>
QVariant parseString<Sink::ApplicationDomain::Mail::Contact>(const QString &s);

template <>
QVariant parseString<QList<Sink::ApplicationDomain::Mail::Contact>>(const QString &s);

class PropertyRegistry
{
public:
    struct Type {
        struct Property {
            std::function<QVariant(const QString &)> parser;
        };
        QHash<QByteArray, Property> properties;
    };

    QHash<QByteArray, Type> registry;

    static PropertyRegistry &instance();

    template <typename PropertyType>
    void registerProperty(const QByteArray &entityType) {
        registry[entityType].properties[PropertyType::name].parser = Sink::Private::parseString<typename PropertyType::Type>;
    }

    QVariant parse(const QByteArray &type, const QByteArray &property, const QString &value);
};

    }
}

