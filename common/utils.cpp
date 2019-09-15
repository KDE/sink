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
#include "utils.h"
#include <QUuid>

QByteArray Sink::createUuid()
{
    return QUuid::createUuid().toByteArray();
}

const QByteArray Sink::sizeTToByteArray(const size_t &value)
{
    return QByteArray::fromRawData(reinterpret_cast<const char *>(&value), sizeof(size_t));
}

size_t Sink::byteArrayToSizeT(const QByteArray &value)
{
    return *reinterpret_cast<const size_t *>(value.constData());
}
