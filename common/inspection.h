/*
 * Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include <QByteArray>
#include <QVariant>
#include "applicationdomaintype.h"

namespace Sink {
namespace ResourceControl {

struct Inspection
{
    static Inspection PropertyInspection(const Sink::ApplicationDomain::Entity &entity, const QByteArray &property, const QVariant &expectedValue)
    {
        Inspection inspection;
        inspection.resourceIdentifier = entity.resourceInstanceIdentifier();
        inspection.entityIdentifier = entity.identifier();
        inspection.property = property;
        inspection.expectedValue = expectedValue;
        inspection.type = PropertyInspectionType;
        return inspection;
    }

    static Inspection ExistenceInspection(const Sink::ApplicationDomain::Entity &entity, bool exists)
    {
        Inspection inspection;
        inspection.resourceIdentifier = entity.resourceInstanceIdentifier();
        inspection.entityIdentifier = entity.identifier();
        inspection.expectedValue = exists;
        inspection.type = ExistenceInspectionType;
        return inspection;
    }

    static Inspection CacheIntegrityInspection(const Sink::ApplicationDomain::Entity &entity)
    {
        Inspection inspection;
        inspection.resourceIdentifier = entity.resourceInstanceIdentifier();
        inspection.entityIdentifier = entity.identifier();
        inspection.type = CacheIntegrityInspectionType;
        return inspection;
    }

    static Inspection ConnectionInspection(const QByteArray &resourceIdentifier)
    {
        Inspection inspection;
        inspection.resourceIdentifier = resourceIdentifier;
        inspection.type = ConnectionInspectionType;
        return inspection;
    }

    enum Type
    {
        PropertyInspectionType,
        ExistenceInspectionType,
        CacheIntegrityInspectionType,
        ConnectionInspectionType,
    };
    QByteArray resourceIdentifier;
    QByteArray entityIdentifier;
    QByteArray property;
    QVariant expectedValue;
    int type;
};
}
}
