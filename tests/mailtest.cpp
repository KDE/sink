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
#include "mailtest.h"

#include <QtTest>

#include <QString>
#include <KMime/Message>

#include "store.h"
#include "resourcecontrol.h"
#include "log.h"
#include "test.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

void MailTest::initTestCase()
{
    Test::initTest();
    Log::setDebugOutputLevel(Sink::Log::Trace);
    resetTestEnvironment();
    auto resource = createResource();
    QVERIFY(!resource.identifier().isEmpty());

    VERIFYEXEC(Store::create(resource));

    mResourceInstanceIdentifier = resource.identifier();
}

void MailTest::cleanup()
{
    VERIFYEXEC(ResourceControl::shutdown(mResourceInstanceIdentifier));
    removeResourceFromDisk(mResourceInstanceIdentifier);
}

void MailTest::init()
{
    qDebug();
    qDebug() << "-----------------------------------------";
    qDebug();
    VERIFYEXEC(ResourceControl::start(mResourceInstanceIdentifier));
}

void MailTest::testCreateModifyDeleteFolder()
{
    QString name = "name";
    QByteArray icon = "icon";

    auto folder = Folder::create(mResourceInstanceIdentifier);
    folder.setName(name);
    folder.setIcon(icon);

    VERIFYEXEC(Store::create(folder));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
    {
        auto job = Store::fetchAll<Folder>(Query::RequestedProperties(QByteArrayList() << Folder::Name::name << Folder::Icon::name))
            .then<void, QList<Folder::Ptr>>([=](const QList<Folder::Ptr> &folders) {
                QCOMPARE(folders.size(), 1);
                auto folder = *folders.first();
                QCOMPARE(folder.getName(), name);
                QCOMPARE(folder.getIcon(), icon);
            });
        VERIFYEXEC(job);
    }

    QString name2 = "name2";
    QByteArray icon2 = "icon2";
    folder.setName(name2);
    folder.setIcon(icon2);

    VERIFYEXEC(Store::modify(folder));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
    {
        auto job = Store::fetchAll<Folder>(Query::RequestedProperties(QByteArrayList() << Folder::Name::name << Folder::Icon::name))
            .then<void, QList<Folder::Ptr>>([=](const QList<Folder::Ptr> &folders) {
                QCOMPARE(folders.size(), 1);
                auto folder = *folders.first();
                QCOMPARE(folder.getName(), name2);
                QCOMPARE(folder.getIcon(), icon2);
            });
        VERIFYEXEC(job);
    }

    VERIFYEXEC(Store::remove(folder));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
    {
        auto job = Store::fetchAll<Folder>(Query::RequestedProperties(QByteArrayList() << Folder::Name::name << Folder::Icon::name))
            .then<void, QList<Folder::Ptr>>([=](const QList<Folder::Ptr> &folders) {
                QCOMPARE(folders.size(), 0);
            });
        VERIFYEXEC(job);
    }
}

void MailTest::testCreateModifyDeleteMail()
{

    const auto subject = QString::fromLatin1("Foobar");

    Query query;
    query.resources << mResourceInstanceIdentifier;
    query.request<Mail::Folder>().request<Mail::Subject>();

    auto folder = Folder::create(mResourceInstanceIdentifier);
    folder.setName("folder");
    VERIFYEXEC(Store::create(folder));

    auto message = KMime::Message::Ptr::create();
    message->subject(true)->fromUnicodeString(subject, "utf8");
    message->assemble();

    auto mail = Mail::create(mResourceInstanceIdentifier);
    mail.setMimeMessage(message->encodedContent());
    mail.setFolder(folder);

    VERIFYEXEC(Store::create(mail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(query.resources));
    {
        auto job = Store::fetchAll<Mail>(Query::RequestedProperties(QByteArrayList() << Mail::Folder::name << Mail::Subject::name))
            .then<void, QList<Mail::Ptr>>([=](const QList<Mail::Ptr> &mails) {
                QCOMPARE(mails.size(), 1);
                auto mail = *mails.first();
                // QCOMPARE(mail.getSubject(), subject);
                QCOMPARE(mail.getFolder(), folder.identifier());
                // TODO test access to mime message

                // return Store::remove(*mail)
                //     .then(ResourceControl::flushReplayQueue(query.resources)) // The change needs to be replayed already
                //     .then(ResourceControl::inspect<Mail>(ResourceControl::Inspection::ExistenceInspection(*mail, false)));
            });
        VERIFYEXEC(job);
    }

    const auto subject2 = QString::fromLatin1("Foobar2");
    auto message2 = KMime::Message::Ptr::create();
    message2->subject(true)->fromUnicodeString(subject2, "utf8");
    message2->assemble();
    mail.setMimeMessage(message2->encodedContent());

    VERIFYEXEC(Store::modify(mail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(query.resources));
    {
        auto job = Store::fetchAll<Mail>(Query::RequestedProperties(QByteArrayList() << Mail::Folder::name << Mail::Subject::name))
            .then<void, QList<Mail::Ptr>>([=](const QList<Mail::Ptr> &mails) {
                QCOMPARE(mails.size(), 1);
                auto mail = *mails.first();
                QCOMPARE(mail.getFolder(), folder.identifier());
                // QCOMPARE(mail.getSubject(), subject);
                // TODO test access to modified mime message

            });
        VERIFYEXEC(job);
    }

    VERIFYEXEC(Store::remove(mail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(query.resources));
    {
        auto job = Store::fetchAll<Mail>(Query::RequestedProperties(QByteArrayList() << Mail::Folder::name << Mail::Subject::name))
            .then<void, QList<Mail::Ptr>>([=](const QList<Mail::Ptr> &mails) {
                QCOMPARE(mails.size(), 0);
            });
        VERIFYEXEC(job);
    }
}

#include "mailtest.moc"
