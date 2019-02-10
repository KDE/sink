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
#include <QFile>
#include <KMime/Message>

#include "store.h"
#include "resourcecontrol.h"
#include "log.h"
#include "test.h"
#include "standardqueries.h"
#include "index.h"
#include "definitions.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

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
    VERIFYEXEC(ResourceControl::shutdown(mResourceInstanceIdentifier));
    removeResourceFromDisk(mResourceInstanceIdentifier);
}

void MailThreadTest::init()
{
    VERIFYEXEC(ResourceControl::start(mResourceInstanceIdentifier));
}


void MailThreadTest::testListThreadLeader()
{
    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Mail::Subject>().request<Mail::MimeMessage>().request<Mail::Folder>().request<Mail::Date>();
    query.sort<Mail::Date>();
    query.reduce<Mail::ThreadId>(Query::Reduce::Selector::max<Mail::Date>()).count("count").collect<Mail::Sender>("senders");

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto mails = Store::read<Mail>(query);
    QCOMPARE(mails.size(), 1);
    QVERIFY(mails.first().getSubject().startsWith(QString("ThreadLeader")));
    auto threadSize = mails.first().getProperty("count").toInt();
    QCOMPARE(threadSize, 2);
    QCOMPARE(mails.first().aggregatedIds().size(), 2);
}

/*
 * Thread:
 * 1.
 *  2.
 *   3.
 *
 * 3. first, should result in a new thread.
 * 1. second, should be merged by subject
 * 2. last, should complete the thread.
 */
void MailThreadTest::testIndexInMixedOrder()
{
    auto folder = Folder::create(mResourceInstanceIdentifier);
    folder.setName("folder");
    VERIFYEXEC(Store::create(folder));

    auto message1 = KMime::Message::Ptr::create();
    message1->subject(true)->fromUnicodeString("1", "utf8");
    message1->messageID(true)->generate("foobar.com");
    message1->date(true)->setDateTime(QDateTime::currentDateTimeUtc());
    message1->assemble();

    auto message2 = KMime::Message::Ptr::create();
    message2->subject(true)->fromUnicodeString("Re: 1", "utf8");
    message2->messageID(true)->generate("foobar.com");
    message2->inReplyTo(true)->appendIdentifier(message1->messageID(true)->identifier());
    message2->date(true)->setDateTime(QDateTime::currentDateTimeUtc().addSecs(1));
    message2->assemble();

    auto message3 = KMime::Message::Ptr::create();
    message3->subject(true)->fromUnicodeString("Re: Re: 1", "utf8");
    message3->messageID(true)->generate("foobar.com");
    message3->inReplyTo(true)->appendIdentifier(message2->messageID(true)->identifier());
    message3->date(true)->setDateTime(QDateTime::currentDateTimeUtc().addSecs(2));
    message3->assemble();

    {
        auto mail = Mail::create(mResourceInstanceIdentifier);
        mail.setMimeMessage(message3->encodedContent(true));
        mail.setFolder(folder);
        VERIFYEXEC(Store::create(mail));
    }
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto query = Sink::StandardQueries::threadLeaders(folder);
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Mail::Subject>().request<Mail::MimeMessage>().request<Mail::Folder>().request<Mail::Date>();

    Mail threadLeader;

    //Ensure we find the thread leader
    {
        auto mails = Store::read<Mail>(query);
        QCOMPARE(mails.size(), 1);
        auto mail = mails.first();
        threadLeader = mail;
        QCOMPARE(mail.getSubject(), QString::fromLatin1("Re: Re: 1"));
    }

    {
        auto mail = Mail::create(mResourceInstanceIdentifier);
        mail.setMimeMessage(message2->encodedContent(true));
        mail.setFolder(folder);
        VERIFYEXEC(Store::create(mail));
    }
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    //Ensure we find the thread leader still
    {
        auto mails = Store::read<Mail>(query);
        QCOMPARE(mails.size(), 1);
        auto mail = mails.first();
        QCOMPARE(mail.getSubject(), QString::fromLatin1("Re: Re: 1"));
    }

    {
        auto mail = Mail::create(mResourceInstanceIdentifier);
        mail.setMimeMessage(message1->encodedContent(true));
        mail.setFolder(folder);
        VERIFYEXEC(Store::create(mail));
    }
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    //Ensure the thread is complete
    {
        auto query = Sink::StandardQueries::completeThread(threadLeader);
        query.request<Mail::Subject>().request<Mail::MimeMessage>().request<Mail::Folder>().request<Mail::Date>();

        auto mails = Store::read<Mail>(query);
        QCOMPARE(mails.size(), 3);
        auto mail = mails.first();
        QCOMPARE(mail.getSubject(), QString::fromLatin1("Re: Re: 1"));
    }

    /* VERIFYEXEC(Store::remove(mail)); */
    /* VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier)); */
    /* { */
    /*     auto job = Store::fetchAll<Mail>(Query::RequestedProperties(QByteArrayList() << Mail::Folder::name << Mail::Subject::name)) */
    /*         .then([=](const QList<Mail::Ptr> &mails) { */
    /*             QCOMPARE(mails.size(), 0); */
    /*         }); */
    /*     VERIFYEXEC(job); */
    /* } */
    /* VERIFYEXEC(ResourceControl::flushReplayQueue(QByteArrayList() << mResourceInstanceIdentifier)); */
}

static QByteArray readMailFromFile(const QString &mailFile)
{
    QFile file(QLatin1String(THREADTESTDATAPATH) + QLatin1Char('/') + mailFile);
    file.open(QIODevice::ReadOnly);
    Q_ASSERT(file.isOpen());
    return file.readAll();
}

static KMime::Message::Ptr readMail(const QString &mailFile)
{
    auto msg = KMime::Message::Ptr::create();
    msg->setContent(readMailFromFile(mailFile));
    msg->parse();
    return msg;
}

void MailThreadTest::testRealWorldThread()
{
    auto folder = Folder::create(mResourceInstanceIdentifier);
    folder.setName("folder");
    VERIFYEXEC(Store::create(folder));

    auto createMail = [this, folder] (KMime::Message::Ptr msg) {
        auto mail = Mail::create(mResourceInstanceIdentifier);
        mail.setMimeMessage(msg->encodedContent(true));
        mail.setFolder(folder);
        VERIFYEXEC(Store::create(mail));
    };

    createMail(readMail("thread1_1"));

    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto query = Sink::StandardQueries::threadLeaders(folder);
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Mail::Subject>().request<Mail::MimeMessage>().request<Mail::Folder>().request<Mail::Date>();

    //Ensure we find the thread leader
    Mail threadLeader = [&] {
        auto mails = Store::read<Mail>(query);
        Q_ASSERT(mails.size() == 1);
        return mails.first();
    }();

    createMail(readMail("thread1_2"));
    createMail(readMail("thread1_3"));
    createMail(readMail("thread1_4"));
    createMail(readMail("thread1_5"));
    createMail(readMail("thread1_6"));
    createMail(readMail("thread1_7"));
    createMail(readMail("thread1_8")); //This mail is breaking the thread
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

    //Ensure the thread is complete
    {
        auto query = Sink::StandardQueries::completeThread(threadLeader);
        query.request<Mail::Subject>().request<Mail::MimeMessage>().request<Mail::Folder>().request<Mail::Date>();

        auto mails = Store::read<Mail>(query);
        QCOMPARE(mails.size(), 8);
    }

    {
        auto query = Sink::StandardQueries::threadLeaders(folder);
        Mail threadLeader2 = [&] {
            auto mails = Store::read<Mail>(query);
            Q_ASSERT(mails.size() == 1);
            return mails.first();
        }();

        {
            auto query = Sink::StandardQueries::completeThread(threadLeader2);
            query.request<Mail::Subject>().request<Mail::MimeMessage>().request<Mail::Folder>().request<Mail::Date>();

            auto mails = Store::read<Mail>(query);
            QCOMPARE(mails.size(), 8);
        }
    }
}

//Avoid accidentally merging or changing threads
void MailThreadTest::testNoParentsWithModifications()
{
    auto folder = Folder::create(mResourceInstanceIdentifier);
    folder.setName("folder2");
    VERIFYEXEC(Store::create(folder));

    auto createMail = [&] (const QString &subject) {
        auto message1 = KMime::Message::Ptr::create();
        message1->subject(true)->fromUnicodeString(subject, "utf8");
        message1->messageID(true)->fromUnicodeString("<" + subject + "@foobar.com" + ">", "utf8");
        message1->date(true)->setDateTime(QDateTime::currentDateTimeUtc());
        message1->assemble();

        auto mail = Mail::create(mResourceInstanceIdentifier);
        mail.setMimeMessage(message1->encodedContent(true));
        mail.setFolder(folder);
        return mail;
    };

    auto mail1 = createMail("1");
    VERIFYEXEC(Store::create(mail1));
    auto mail2 = createMail("2");
    VERIFYEXEC(Store::create(mail2));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

    auto query = Sink::StandardQueries::threadLeaders(folder);
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Mail::Subject>().request<Mail::MimeMessage>().request<Mail::Folder>().request<Mail::Date>().request<Mail::ThreadId>();

    QSet<QByteArray> threadIds;
    {
        auto mails = Store::read<Mail>(query);
        QCOMPARE(mails.size(), 2);
        for (const auto &m : mails) {
            threadIds << m.getProperty(Mail::ThreadId::name).toByteArray();
        }
    }

    auto readIndex = [&] (const QString &indexName, const QByteArray &lookupKey) {
        Index index(Sink::storageLocation(), mResourceInstanceIdentifier, indexName, Sink::Storage::DataStore::ReadOnly);
        QByteArrayList keys;
        index.lookup(lookupKey,
            [&](const QByteArray &value) { keys << QByteArray{value.constData(), value.size()}; },
            [=](const Index::Error &error) { SinkWarning() << "Lookup error in secondary index: " << error.message; },
            false);
        return keys;
    };
    QCOMPARE(readIndex("mail.index.messageIdthreadId", "1@foobar.com").size(), 1);
    QCOMPARE(readIndex("mail.index.messageIdthreadId", "2@foobar.com").size(), 1);

    //We try to modify both mails on purpose
    auto checkMail = [&] (Mail mail1) {
        Mail modification = mail1;
        modification.setChangedProperties({});
        modification.setImportant(true);
        VERIFYEXEC(Store::modify(modification));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        QCOMPARE(readIndex("mail.index.messageIdthreadId", "1@foobar.com").size(), 1);
        QCOMPARE(readIndex("mail.index.messageIdthreadId", "2@foobar.com").size(), 1);

        {
            auto mails = Store::read<Mail>(query);
            QCOMPARE(mails.size(), 2);
            QSet<QByteArray> newThreadIds;
            for (const auto &m : mails) {
                newThreadIds << m.getProperty(Mail::ThreadId::name).toByteArray();
            }
            QCOMPARE(threadIds, newThreadIds);
        }
    };
    checkMail(mail1);
    checkMail(mail2);
}


void MailThreadTest::testRealWorldThread2()
{
    auto folder = Folder::create(mResourceInstanceIdentifier);
    folder.setName("folder2");
    VERIFYEXEC(Store::create(folder));

    auto createMail = [this, folder] (KMime::Message::Ptr msg) {
        auto mail = Mail::create(mResourceInstanceIdentifier);
        mail.setMimeMessage(msg->encodedContent(true));
        mail.setFolder(folder);
        VERIFYEXEC(Store::create(mail));
    };

    createMail(readMail(QString("thread2_%1").arg(1))); //30.10.18
    createMail(readMail(QString("thread2_%1").arg(2))); //02.11.18
    createMail(readMail(QString("thread2_%1").arg(3))); //07.11.18
    createMail(readMail(QString("thread2_%1").arg(4))); //09.11.18
    createMail(readMail(QString("thread2_%1").arg(14))); //13.11.18
    createMail(readMail(QString("thread2_%1").arg(12))); //16.11.18
    createMail(readMail(QString("thread2_%1").arg(6))); //16.11.18
    createMail(readMail(QString("thread2_%1").arg(9))); //23.11.18
    // createMail(readMail(QString("thread2_%1").arg(i))); //Different thread 18.1
    createMail(readMail(QString("thread2_%1").arg(7))); //04.12.18
    createMail(readMail(QString("thread2_%1").arg(17))); //18.12.18
    createMail(readMail(QString("thread2_%1").arg(13))); //22.1
    createMail(readMail(QString("thread2_%1").arg(15))); //25.1
    createMail(readMail(QString("thread2_%1").arg(11))); //28.1
    createMail(readMail(QString("thread2_%1").arg(10))); //29.1
    createMail(readMail(QString("thread2_%1").arg(16))); //29.1


    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

    //Ensure we only got one thread
    const auto mails = Store::read<Mail>(Sink::StandardQueries::threadLeaders(folder));
    QCOMPARE(mails.size(), 1);

    //Ensure the thread is complete
    QCOMPARE(Store::read<Mail>(Sink::StandardQueries::completeThread(mails.first())).size(), 15);
}

