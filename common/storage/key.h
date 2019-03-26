/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
 * Copyright (C) 2018 Rémi Nicole <minijackson@riseup.net>
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

#include <QByteArray>
#include <QDebug>
#include <QUuid>

namespace Sink {
namespace Storage {

class SINK_EXPORT Identifier
{
public:
    // RFC 4122 Section 4.1.2 says 128 bits -> 16 bytes
    static const constexpr size_t INTERNAL_REPR_SIZE = 16;
    static const constexpr size_t DISPLAY_REPR_SIZE = 38;

    Identifier() = default;
    static Identifier createIdentifier();

    QByteArray toInternalByteArray() const;
    static Identifier fromInternalByteArray(const QByteArray &bytes);
    QString toDisplayString() const;
    QByteArray toDisplayByteArray() const;
    static Identifier fromDisplayByteArray(const QByteArray &bytes);

    bool isNull() const;

    static bool isValidInternal(const QByteArray &);
    static bool isValidDisplay(const QByteArray &);
    static bool isValid(const QByteArray &);

    bool operator==(const Identifier &other) const;
    bool operator!=(const Identifier &other) const;

private:
    explicit Identifier(const QUuid &uid) : uid(uid) {}
    QUuid uid;
};

class SINK_EXPORT Revision
{
public:
    // qint64 has a 19 digit decimal representation
    static const constexpr size_t INTERNAL_REPR_SIZE = 19;
    static const constexpr size_t DISPLAY_REPR_SIZE = 19;

    Revision(size_t rev) : rev(rev) {}

    QByteArray toInternalByteArray() const;
    static Revision fromInternalByteArray(const QByteArray &bytes);
    QString toDisplayString() const;
    QByteArray toDisplayByteArray() const;
    static Revision fromDisplayByteArray(const QByteArray &bytes);
    qint64 toQint64() const;
    size_t toSizeT() const;

    static bool isValidInternal(const QByteArray &);
    static bool isValidDisplay(const QByteArray &);
    static bool isValid(const QByteArray &);

    bool operator==(const Revision &other) const;
    bool operator!=(const Revision &other) const;

private:
    size_t rev;
};

class SINK_EXPORT Key
{
public:
    static const constexpr size_t INTERNAL_REPR_SIZE = Identifier::INTERNAL_REPR_SIZE + Revision::INTERNAL_REPR_SIZE;
    static const constexpr size_t DISPLAY_REPR_SIZE = Identifier::DISPLAY_REPR_SIZE + Revision::DISPLAY_REPR_SIZE;

    Key() : id(), rev(0) {}
    Key(const Identifier &id, const Revision &rev) : id(id), rev(rev) {}

    QByteArray toInternalByteArray() const;
    static Key fromInternalByteArray(const QByteArray &bytes);
    QString toDisplayString() const;
    QByteArray toDisplayByteArray() const;
    static Key fromDisplayByteArray(const QByteArray &bytes);
    const Identifier &identifier() const;
    const Revision &revision() const;
    void setRevision(const Revision &newRev);

    bool isNull() const;

    static bool isValidInternal(const QByteArray &);
    static bool isValidDisplay(const QByteArray &);
    static bool isValid(const QByteArray &);

    bool operator==(const Key &other) const;
    bool operator!=(const Key &other) const;

private:
    Identifier id;
    Revision rev;
};

SINK_EXPORT uint qHash(const Sink::Storage::Identifier &);

} // namespace Storage
} // namespace Sink

SINK_EXPORT QDebug &operator<<(QDebug &dbg, const Sink::Storage::Identifier &);
SINK_EXPORT QDebug &operator<<(QDebug &dbg, const Sink::Storage::Revision &);
SINK_EXPORT QDebug &operator<<(QDebug &dbg, const Sink::Storage::Key &);
