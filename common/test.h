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

#include "sink_export.h"
#include "applicationdomaintype.h"

#include <memory>

namespace Sink {
namespace Test {
/**
 * Initialize the test environment.
 * 
 * This makes use of QStandardPaths::setTestModeEnabled to avoid writing to the user directory,
 * and clears all data directories.
 */
void SINK_EXPORT initTest();

class SINK_EXPORT TestAccount {
public:
    QByteArray identifier;
    static TestAccount registerAccount();

    template<typename DomainType>
    void addEntity(const ApplicationDomain::ApplicationDomainType::Ptr &domainObject);

    template<typename DomainType>
    typename DomainType::Ptr createEntity();

    template<typename DomainType>
    QList<ApplicationDomain::ApplicationDomainType::Ptr> entities() const;

private:
    TestAccount(){};
    TestAccount(const TestAccount &);
    TestAccount &operator=(const TestAccount &);
    QHash<QByteArray, QList<ApplicationDomain::ApplicationDomainType::Ptr> > mEntities;
    QHash<QByteArray, std::shared_ptr<void> > mFacades;
};

}
}
