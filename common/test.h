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
void SINK_EXPORT setTestModeEnabled(bool);
bool SINK_EXPORT testModeEnabled();

class SINK_EXPORT TestAccount {
public:
    QByteArray identifier;
    static TestAccount registerAccount();

    template<typename DomainType>
    void SINK_EXPORT addEntity(const ApplicationDomain::ApplicationDomainType::Ptr &domainObject);

    template<typename DomainType>
    typename DomainType::Ptr SINK_EXPORT createEntity();

    template<typename DomainType>
    QList<ApplicationDomain::ApplicationDomainType::Ptr> SINK_EXPORT entities() const;

private:
    TestAccount(){};
    TestAccount(const TestAccount &);
    TestAccount &operator=(const TestAccount &);
    QHash<QByteArray, QList<ApplicationDomain::ApplicationDomainType::Ptr> > mEntities;
    QHash<QByteArray, std::shared_ptr<void> > mFacades;
};

}
}

#define ASYNCCOMPARE(actual, expected) \
do {\
    if (!QTest::qCompare(actual, expected, #actual, #expected, __FILE__, __LINE__))\
        return KAsync::error<void>(1, "Comparison failed.");\
} while (0)

#define ASYNCVERIFY(statement) \
do {\
    if (!QTest::qVerify((statement), #statement, "", __FILE__, __LINE__))\
        return KAsync::error<void>(1, "Verify failed.");\
} while (0)

#define VERIFYEXEC(statement) \
do {\
    auto result = statement.exec(); \
    result.waitForFinished(); \
    if (!QTest::qVerify(!result.errorCode(), #statement, "", __FILE__, __LINE__))\
        return;\
} while (0)

#define VERIFYEXEC_RET(statement, return) \
do {\
    auto result = statement.exec(); \
    result.waitForFinished(); \
    if (!QTest::qVerify(!result.errorCode(), #statement, "", __FILE__, __LINE__))\
        return #return;\
} while (0)

#define VERIFYEXEC_FAIL(statement) \
do {\
    auto result = statement.exec(); \
    result.waitForFinished(); \
    if (!QTest::qVerify(result.errorCode(), #statement, "", __FILE__, __LINE__))\
        return;\
} while (0)

//qWait(1) seems to simply skip waiting at all.
#define QUICK_TRY_VERIFY(statement) \
do {\
    static int timeout = 2500; \
    int i = 0; \
    for (; i < timeout && !(statement); i++) { \
        QTest::qWait(2); \
    } \
    if (i >= timeout) { \
        qWarning() << "Timeout during QUICK_TRY_VERIFY"; \
    } \
} while (0)
