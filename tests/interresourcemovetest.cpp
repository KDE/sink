/*
 * Copyright (C) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
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
#include <QTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "store.h"
#include "resourceconfig.h"
#include "resourcecontrol.h"
#include "log.h"
#include "test.h"
#include "test.h"
#include <KMime/Message>

using namespace Sink;
using namespace Sink::ApplicationDomain;

/**
 * Test of complete system using the dummy resource.
 *
 * This test requires the dummy resource installed.
 */
class InterResourceMoveTest : public QObject
{
    Q_OBJECT

    QByteArray message(const QByteArray &uid, const QString &subject)
    {
        KMime::Message m;
        m.subject(true)->fromUnicodeString(subject, "utf8");
        m.messageID(true)->setIdentifier(uid);
        m.assemble();
        return m.encodedContent(true);
    }

private slots:
    void initTestCase()
    {
        Sink::Test::initTest();
        auto factory = Sink::ResourceFactory::load("sink.dummy");
        QVERIFY(factory);
        ::DummyResource::removeFromDisk("instance1");
        ::DummyResource::removeFromDisk("instance2");
        ResourceConfig::addResource("instance1", "sink.dummy");
        ResourceConfig::addResource("instance2", "sink.dummy");
    }

    void init()
    {
    }

    void cleanup()
    {
        VERIFYEXEC(Sink::Store::removeDataFromDisk(QByteArray("instance1")));
        VERIFYEXEC(Sink::Store::removeDataFromDisk(QByteArray("instance2")));
    }

    void testMove()
    {
        QByteArray testuid = "testuid@test.test";
        QString subject = "summaryValue";
        auto mimeMessage = message(testuid, subject);

        Mail mail("instance1");
        mail.setMimeMessage(mimeMessage);
        VERIFYEXEC(Sink::Store::create<Mail>(mail));

        Mail createdmail;
        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("instance1"));
        {
            auto query = Query().resourceFilter("instance1") ;
            auto list = Sink::Store::read<Mail>(query.filter<Mail::MessageId>(testuid));
            QCOMPARE(list.size(), 1);
            createdmail = list.first();
        }

        VERIFYEXEC(Sink::Store::move<Mail>(createdmail, "instance2"));

        //FIXME we can't guarantee that that the create command arrives at instance2 before the flush command, so we'll just wait for a little bit.
        QTest::qWait(1000);
        //Ensure the move has been processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("instance1"));
        //Ensure the create in the target resource has been processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("instance2"));
        {
            auto query = Query().resourceFilter("instance2") ;
            auto list = Sink::Store::read<Mail>(query.filter<Mail::MessageId>(testuid));
            QCOMPARE(list.size(), 1);
            const auto mail = list.first();
            QCOMPARE(mail.getSubject(), subject);
            QCOMPARE(mail.getMimeMessage(), mimeMessage);
        }

        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("instance1"));
        {
            auto query = Query().resourceFilter("instance1") ;
            auto list = Sink::Store::read<Mail>(query.filter<Mail::MessageId>(testuid));
            QCOMPARE(list.size(), 0);
        }
    }

    void testCopy()
    {
        QByteArray testuid = "testuid@test.test";
        QString subject = "summaryValue";
        auto mimeMessage = message(testuid, subject);

        Mail mail("instance1");
        mail.setMimeMessage(mimeMessage);
        VERIFYEXEC(Sink::Store::create<Mail>(mail));


        Mail createdMail;
        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "instance1"));
        {
            auto query = Query().resourceFilter("instance1") ;
            auto list = Sink::Store::read<Mail>(query.filter<Mail::MessageId>(testuid));
            QCOMPARE(list.size(), 1);
            createdMail = list.first();
        }

        VERIFYEXEC(Sink::Store::copy<Mail>(createdMail, "instance2"));

        //FIXME we can't guarantee that that the create command arrives at instance2 before the flush command, so we'll just wait for a little bit.
        QTest::qWait(100);
        //Ensure the copy has been processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "instance1"));
        //Ensure the create in the target resource has been processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "instance2"));
        {
            auto query = Query().resourceFilter("instance2") ;
            auto list = Sink::Store::read<Mail>(query.filter<Mail::MessageId>(testuid));
            QCOMPARE(list.size(), 1);
            const auto mail = list.first();
            QCOMPARE(mail.getSubject(), subject);
            QCOMPARE(mail.getMimeMessage(), mimeMessage);
        }

        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "instance1"));
        {
            auto query = Query().resourceFilter("instance1") ;
            auto list = Sink::Store::read<Mail>(query.filter<Mail::MessageId>(testuid));
            QCOMPARE(list.size(), 1);
            const auto mail = list.first();
            QCOMPARE(mail.getSubject(), subject);
            QCOMPARE(mail.getMimeMessage(), mimeMessage);
        }
    }

};

QTEST_MAIN(InterResourceMoveTest)
#include "interresourcemovetest.moc"
