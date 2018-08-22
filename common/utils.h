/*
    Copyright (c) 2018 Christian Mollekopf <mollekopf@kolabsys.com>

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

#include <QByteArray>

#include <cmath>

namespace Sink {

QByteArray createUuid();

// No copy is done on this functions. Therefore, the caller must not use the
// returned QByteArray after the size_t has been destroyed.
const QByteArray sizeTToByteArray(const size_t &);
size_t byteArrayToSizeT(const QByteArray &);

template <typename T>
static QByteArray padNumber(T number);

template <>
QByteArray padNumber<size_t>(size_t number)
{
    return padNumber<qint64>(number);
}

template <typename T>
static QByteArray padNumber(T number)
{
    static T uint_num_digits = (T)std::log10(std::numeric_limits<T>::max()) + 1;
    return QByteArray::number(number).rightJustified(uint_num_digits, '0');
}

} // namespace Sink
