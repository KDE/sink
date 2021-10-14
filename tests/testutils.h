/*
 *   Copyright (C) 2016 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#pragma once

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
