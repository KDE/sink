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
#include "propertyregistry.h"

#include <log.h>

namespace Sink {
namespace Private {

template <typename T>
QVariant parseString(const QString &);

template <>
QVariant parseString<QString>(const QString &s)
{
    return QVariant::fromValue(s);
}

template <>
QVariant parseString<QByteArray>(const QString &s)
{
    return QVariant::fromValue(s.toUtf8());
}

template <>
QVariant parseString<Sink::ApplicationDomain::Reference>(const QString &s)
{
    return QVariant::fromValue(Sink::ApplicationDomain::Reference{s.toLatin1()});
}

template <>
QVariant parseString<bool>(const QString &s)
{
    if (s == "true") {
        return QVariant::fromValue(true);
    }
    return QVariant::fromValue(false);
}

template <>
QVariant parseString<int>(const QString &s)
{
    bool ok = false;
    auto n = s.toInt(&ok);
    if (ok) {
        return QVariant::fromValue(n);
    }
    return {};
}

template <>
QVariant parseString<QList<QByteArray>>(const QString &s)
{
    auto list = s.split(',');
    QList<QByteArray> result;
    std::transform(list.constBegin(), list.constEnd(), std::back_inserter(result), [] (const QString &s) { return s.toUtf8(); });
    return QVariant::fromValue(result);
}

template <>
QVariant parseString<QDateTime>(const QString &s)
{
    return QVariant::fromValue(QDateTime::fromString(s));
}

template <>
QVariant parseString<Sink::ApplicationDomain::Mail::Contact>(const QString &s)
{
    Q_ASSERT(false);
    return QVariant{};
}

template <>
QVariant parseString<QList<Sink::ApplicationDomain::Mail::Contact>>(const QString &s)
{
    Q_ASSERT(false);
    return QVariant{};
}

template <>
QVariant parseString<QList<Sink::ApplicationDomain::Contact::Email>>(const QString &s)
{
    Q_ASSERT(false);
    return QVariant{};
}

PropertyRegistry &PropertyRegistry::instance()
{
    static PropertyRegistry instance;
    return instance;
}

QVariant PropertyRegistry::parse(const QByteArray &type, const QByteArray &property, const QString &value)
{
    auto parser = registry[type].properties[property].parser;
    if (parser) {
        return parser(value);
    }
    SinkWarningCtx(Sink::Log::Context{"PropertyRegistry"}) << "Couldn't find a parser for " << type << property;
    return QVariant{};
}

    }
}
