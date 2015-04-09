/*
 * Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "domainadaptor.h"

template <>
QSharedPointer<PropertyMapper<Akonadi2::ApplicationDomain::Buffer::Event> > initializePropertyMapper<Akonadi2::ApplicationDomain::Buffer::Event>()
{
    auto propertyMapper = QSharedPointer<PropertyMapper<Akonadi2::ApplicationDomain::Buffer::Event> >::create();
    propertyMapper->mReadAccessors.insert("summary", [](Akonadi2::ApplicationDomain::Buffer::Event const *buffer) -> QVariant {
        if (buffer->summary()) {
            return QString::fromStdString(buffer->summary()->c_str());
        }
        return QVariant();
    });
    propertyMapper->mReadAccessors.insert("uid", [](Akonadi2::ApplicationDomain::Buffer::Event const *buffer) -> QVariant {
        if (buffer->uid()) {
            return QString::fromStdString(buffer->uid()->c_str());
        }
        return QVariant();
    });
    return propertyMapper;
}

