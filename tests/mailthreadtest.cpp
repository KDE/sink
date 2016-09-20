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
#include "mailthreadtest.h"

#include <QtTest>

#include <QString>
#include <KMime/Message>

#include "store.h"
#include "resourcecontrol.h"
#include "log.h"
#include "test.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

SINK_DEBUG_AREA("mailthreadtest")

//TODO extract resource test
//
void MailThreadTest::initTestCase()
{
    Test::initTest();
    QVERIFY(isBackendAvailable());
    resetTestEnvironment();
    auto resource = createResource();
    QVERIFY(!resource.identifier().isEmpty());

    VERIFYEXEC(Store::create(resource));

    mResourceInstanceIdentifier = resource.identifier();
    mCapabilities = resource.getProperty("capabilities").value<QByteArrayList>();
}

void MailThreadTest::cleanup()
{
    //TODO the shutdown job fails if the resource is already shut down
    // VERIFYEXEC(ResourceControl::shutdown(mResourceInstanceIdentifier));
    ResourceControl::shutdown(mResourceInstanceIdentifier).exec().waitForFinished();
    removeResourceFromDisk(mResourceInstanceIdentifier);
}

void MailThreadTest::init()
{
    VERIFYEXEC(ResourceControl::start(mResourceInstanceIdentifier));
}


void MailThreadTest::testListThreadLeader()
{
    Sink::Query query;
    query.resources << mResourceInstanceIdentifier;
    query.request<Mail::Subject>().request<Mail::MimeMessage>().request<Mail::Folder>().request<Mail::Date>();
    query.threadLeaderOnly = true;
    query.sort<Mail::Date>();

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

    auto job = Store::fetchAll<Mail>(query).syncThen<void, QList<Mail::Ptr>>([](const QList<Mail::Ptr> &mails) {
        QCOMPARE(mails.size(), 1);
        QVERIFY(mails.first()->getSubject().startsWith(QString("ThreadLeader")));
        const auto data = mails.first()->getMimeMessage();
        QVERIFY(!data.isEmpty());

        KMime::Message m;
        m.setContent(data);
        m.parse();
        QCOMPARE(mails.first()->getSubject(), m.subject(true)->asUnicodeString());
        QVERIFY(!mails.first()->getFolder().isEmpty());
        QVERIFY(mails.first()->getDate().isValid());
    });
    VERIFYEXEC(job);
}

#include "mailthreadtest.moc"
