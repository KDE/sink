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
#include "mailsynctest.h"

#include <QTest>

#include <QString>
#include <KMime/Message>

#include "store.h"
#include "resourcecontrol.h"
#include "notifier.h"
#include "notification.h"
#include "log.h"
#include "test.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

static QByteArray newMessage(const QString &subject)
{
    auto msg = KMime::Message::Ptr::create();
    msg->subject(true)->fromUnicodeString(subject, "utf8");
    msg->date(true)->setDateTime(QDateTime::currentDateTimeUtc());
    msg->assemble();
    return msg->encodedContent(true);
}


void MailSyncTest::initTestCase()
{
    Test::initTest();
    QVERIFY(isBackendAvailable());
    resetTestEnvironment();
    auto resource = createResource();
    QVERIFY(!resource.identifier().isEmpty());

    VERIFYEXEC(Store::create(resource));

    mResourceInstanceIdentifier = resource.identifier();
    //Load the capabilities
    resource = Store::readOne<Sink::ApplicationDomain::SinkResource>(Sink::Query{resource});
    mCapabilities = resource.getCapabilities();
}

void MailSyncTest::cleanup()
{
    VERIFYEXEC(ResourceControl::shutdown(mResourceInstanceIdentifier));
    removeResourceFromDisk(mResourceInstanceIdentifier);
}

void MailSyncTest::init()
{
    VERIFYEXEC(ResourceControl::start(mResourceInstanceIdentifier));
}

void MailSyncTest::testListFolders()
{
    int baseCount = 0;
    //First figure out how many folders we have by default
    {
        auto job = Store::fetchAll<Folder>(Query())
            .then([&](const QList<Folder::Ptr> &folders) {
                QStringList names;
                for (const auto &folder : folders) {
                    names << folder->getName();
                }
                SinkTrace() << "base folder: " << names;
                baseCount = folders.size();
            });
        VERIFYEXEC(job);
    }

    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Folder::Name>().request<Folder::SpecialPurpose>();

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto job = Store::fetchAll<Folder>(query).then([=](const QList<Folder::Ptr> &folders) {
        QStringList names;
        QHash<QByteArray, QByteArray> specialPurposeFolders;
        for (const auto &folder : folders) {
            names << folder->getName();
            for (const auto &purpose : folder->getSpecialPurpose()) {
                specialPurposeFolders.insert(purpose, folder->identifier());
            }
        }
        //Workaround for maildir
        if (names.contains("maildir1")) {
            names.removeAll("maildir1");
        }
        if (mCapabilities.contains(ResourceCapabilities::Mail::drafts)) {
            QVERIFY(names.contains("Drafts"));
            names.removeAll("Drafts");
            QVERIFY(specialPurposeFolders.contains(SpecialPurpose::Mail::drafts));
        }
        if (mCapabilities.contains(ResourceCapabilities::Mail::trash)) {
            QVERIFY(names.contains("Trash"));
            names.removeAll("Trash");
            QVERIFY(specialPurposeFolders.contains(SpecialPurpose::Mail::trash));
        }
        auto set = QSet<QString>{"INBOX", "test"};
        QCOMPARE(names.toSet(), set);
    });
    VERIFYEXEC(job);
}

void MailSyncTest::testListNewFolder()
{
    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Folder::Name>();

    createFolder(QStringList() << "test2");

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto job = Store::fetchAll<Folder>(query).then([](const QList<Folder::Ptr> &folders) {
        QStringList names;
        for (const auto &folder : folders) {
            names << folder->getName();
        }
        QVERIFY(names.contains("test2"));
    });
    VERIFYEXEC(job);
}

void MailSyncTest::testListRemovedFolder()
{
    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Folder::Name>();

    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    removeFolder(QStringList() << "test2");

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto job = Store::fetchAll<Folder>(query).then([](const QList<Folder::Ptr> &folders) {
        QStringList names;
        for (const auto &folder : folders) {
            names << folder->getName();
        }
        QVERIFY(!names.contains("test2"));
    });
    VERIFYEXEC(job);
}

void MailSyncTest::testListRemovedFullFolder()
{
    createFolder({"testRemoval"});
    createMessage({"testRemoval"}, newMessage("mailToRemove"));

    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Folder::Name>();

    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
    QCOMPARE(Sink::Store::read<Folder>(Sink::Query{}.filter<Folder::Name>("testRemoval")).size(), 1);
    QCOMPARE(Sink::Store::read<Mail>(Sink::Query{}.filter<Mail::Subject>("mailToRemove")).size(), 1);

    removeFolder({"testRemoval"});

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

    QCOMPARE(Sink::Store::read<Folder>(Sink::Query{}.filter<Folder::Name>("testRemoval")).size(), 0);
    QCOMPARE(Sink::Store::read<Mail>(Sink::Query{}.filter<Mail::Subject>("mailToRemove")).size(), 0);
}

void MailSyncTest::testListFolderHierarchy()
{
    if (!mCapabilities.contains(ResourceCapabilities::Mail::folderhierarchy)) {
        QSKIP("Missing capability folder.hierarchy");
    }
    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Folder::Name>().request<Folder::Parent>();

    createFolder(QStringList() << "test" << "sub");

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto job = Store::fetchAll<Folder>(query).then([=](const QList<Folder::Ptr> &folders) {
        QHash<QString, Folder::Ptr> map;
        for (const auto &folder : folders) {
            map.insert(folder->getName(), folder);
        }
        QStringList names;
        for (const auto &folder : folders) {
            names << folder->getName();
        }

        //Workaround for maildir
        if (names.contains("maildir1")) {
            names.removeAll("maildir1");
        }
        if (mCapabilities.contains(ResourceCapabilities::Mail::drafts)) {
            QVERIFY(names.contains("Drafts"));
            names.removeAll("Drafts");
        }
        if (mCapabilities.contains(ResourceCapabilities::Mail::trash)) {
            QVERIFY(names.contains("Trash"));
            names.removeAll("Trash");
        }
        QCOMPARE(names.size(), 3);
        QCOMPARE(map.value("sub")->getParent(), map.value("test")->identifier());
    });
    VERIFYEXEC(job);
}

void MailSyncTest::testListNewSubFolder()
{
    if (!mCapabilities.contains(ResourceCapabilities::Mail::folderhierarchy)) {
        QSKIP("Missing capability mail.folderhierarchy");
    }
    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Folder::Name>();

    createFolder(QStringList() << "test" << "sub1");

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto job = Store::fetchAll<Folder>(query).then([](const QList<Folder::Ptr> &folders) {
        QStringList names;
        for (const auto &folder : folders) {
            names << folder->getName();
        }
        ASYNCVERIFY(names.contains("sub1"));
        return KAsync::null();
    });
    VERIFYEXEC(job);
}

void MailSyncTest::testListRemovedSubFolder()
{
    if (!mCapabilities.contains(ResourceCapabilities::Mail::folderhierarchy)) {
        QSKIP("Missing capability folder.hierarchy");
    }
    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Folder::Name>();

    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    removeFolder(QStringList() << "test" << "sub1");

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto job = Store::fetchAll<Folder>(query).then([](const QList<Folder::Ptr> &folders) {
        QStringList names;
        for (const auto &folder : folders) {
            names << folder->getName();
        }
        ASYNCVERIFY(!names.contains("sub1"));
        return KAsync::null();
    });
    VERIFYEXEC(job);
}

void MailSyncTest::testListMails()
{
    createMessage(QStringList() << "test", newMessage("This is a Subject."));

    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Mail::Subject>().request<Mail::MimeMessage>().request<Mail::Folder>().request<Mail::Date>();

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto job = Store::fetchAll<Mail>(query).then([](const QList<Mail::Ptr> &mails) {
        ASYNCCOMPARE(mails.size(), 1);
        auto mail = mails.first();
        ASYNCVERIFY(mail->getSubject().startsWith(QString("This is a Subject.")));
        const auto data = mail->getMimeMessage();
        ASYNCVERIFY(!data.isEmpty());

        KMime::Message m;
        m.setContent(KMime::CRLFtoLF(data));
        m.parse();
        ASYNCCOMPARE(mail->getSubject(), m.subject(true)->asUnicodeString());
        ASYNCVERIFY(!mail->getFolder().isEmpty());
        ASYNCVERIFY(mail->getDate().isValid());
        return KAsync::null();
    });
    VERIFYEXEC(job);
}

void MailSyncTest::testResyncMails()
{
    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Mail::MimeMessage>();
    query.request<Mail::Subject>();

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto job = Store::fetchAll<Mail>(query).then([](const QList<Mail::Ptr> &mails) {
        ASYNCCOMPARE(mails.size(), 1);
        auto mail = mails.first();
        ASYNCVERIFY(!mail->getSubject().isEmpty());
        ASYNCVERIFY(!mail->getMimeMessage().isEmpty());
        return KAsync::null();
    });
    VERIFYEXEC(job);
}

void MailSyncTest::testFetchNewRemovedMessages()
{
    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Mail::Subject>().request<Mail::MimeMessage>();

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto messageIdentifier = createMessage(QStringList() << "test", newMessage("Foobar"));

    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    {
        auto job = Store::fetchAll<Mail>(query).then([](const QList<Mail::Ptr> &mails) {
            ASYNCCOMPARE(mails.size(), 2);
            return KAsync::null();
        });
        VERIFYEXEC(job);
    }

    removeMessage(QStringList() << "test", messageIdentifier);

    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    {
        auto job = Store::fetchAll<Mail>(query).then([](const QList<Mail::Ptr> &mails) {
            ASYNCCOMPARE(mails.size(), 1);
            return KAsync::null();
        });
        VERIFYEXEC(job);
    }
}

void MailSyncTest::testFlagChange()
{
    Sink::Query syncScope;
    syncScope.resourceFilter(mResourceInstanceIdentifier);

    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.filter<Mail::Important>(true);
    query.filter<Mail::Folder>(Sink::Query{}.filter<Folder::Name>("test"));
    query.request<Mail::Subject>().request<Mail::Important>();

    auto messageIdentifier = createMessage(QStringList() << "test", newMessage("Foobar"));

    VERIFYEXEC(Store::synchronize(syncScope));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

    QCOMPARE(Store::read<Mail>(query).size(), 0);

    markAsImportant(QStringList() << "test", messageIdentifier);

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(syncScope));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

    QCOMPARE(Store::read<Mail>(query).size(), 1);
}

void MailSyncTest::testSyncSingleFolder()
{
    VERIFYEXEC(Store::synchronize(Sink::SyncScope{ApplicationDomain::getTypeName<Folder>()}.resourceFilter(mResourceInstanceIdentifier)));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

    Folder::Ptr folder;
    {
        auto job = Store::fetchAll<Folder>(Sink::Query{}.resourceFilter(mResourceInstanceIdentifier).filter<Folder::Name>("test")).template then([&](const QList<Folder::Ptr> &folders) {
            ASYNCCOMPARE(folders.size(), 1);
            folder = folders.first();
            return KAsync::null();
        });
        VERIFYEXEC(job);
    }

    auto syncScope = Sink::SyncScope{ApplicationDomain::getTypeName<Mail>()};
    syncScope.resourceFilter(mResourceInstanceIdentifier);
    syncScope.filter<Mail::Folder>(QVariant::fromValue(folder->identifier()));

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(syncScope));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
}

void MailSyncTest::testSyncSingleMail()
{
    VERIFYEXEC(Store::synchronize(Sink::SyncScope{}.resourceFilter(mResourceInstanceIdentifier)));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

    Mail::Ptr mail;
    {
        auto job = Store::fetchAll<Mail>(Sink::Query{}.resourceFilter(mResourceInstanceIdentifier)).template then([&](const QList<Mail::Ptr> &mails) {
            ASYNCVERIFY(mails.size() >= 1);
            mail = mails.first();
            return KAsync::null();
        });
        VERIFYEXEC(job);
    }
    QVERIFY(mail);

    auto syncScope = Sink::SyncScope{ApplicationDomain::getTypeName<Mail>()};
    syncScope.resourceFilter(mResourceInstanceIdentifier);
    syncScope.filter(mail->identifier());

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(syncScope));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
}

void MailSyncTest::testSyncSingleMailWithBogusId()
{
    VERIFYEXEC(Store::synchronize(Sink::SyncScope{}.resourceFilter(mResourceInstanceIdentifier)));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

    auto syncScope = Sink::SyncScope{ApplicationDomain::getTypeName<Mail>()};
    syncScope.resourceFilter(mResourceInstanceIdentifier);
    syncScope.filter("WTFisThisEven?");

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(syncScope));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
}

void MailSyncTest::testFailingSync()
{
    auto resource = createFaultyResource();
    QVERIFY(!resource.identifier().isEmpty());
    VERIFYEXEC(Store::create(resource));

    Sink::Query query;
    query.resourceFilter(resource.identifier());

    bool errorReceived = false;

    //Wait for the error notifiction
    auto notifier = QSharedPointer<Sink::Notifier>::create(resource.identifier());
    notifier->registerHandler([&](const Notification &notification) {
        SinkTrace() << "Received notification " << notification;
        //Maildir detects misconfiguration, imap fails to connect
        if (notification.type == Sink::Notification::Error &&
                (notification.code == ApplicationDomain::ConnectionError ||
                 notification.code == ApplicationDomain::ConfigurationError)) {
            errorReceived = true;
        }
    });

    VERIFYEXEC(Store::synchronize(query));
    // Ensure sync fails if resource is misconfigured. We have to wait longer than the timeout in imapserverproxy
    QTRY_VERIFY_WITH_TIMEOUT(errorReceived, 10000);
}

void MailSyncTest::testSyncUidvalidity()
{
    createFolder({"uidvalidity"});
    createMessage({"uidvalidity"}, newMessage("old"));

    VERIFYEXEC(Store::synchronize(SyncScope{ApplicationDomain::getTypeName<Folder>()}.resourceFilter(mResourceInstanceIdentifier)));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

    auto folder = Store::readOne<Folder>(Query{}.resourceFilter(mResourceInstanceIdentifier).filter<Folder::Name>("uidvalidity"));

    auto folderSyncScope = SyncScope{ApplicationDomain::getTypeName<Mail>()};
    folderSyncScope.resourceFilter(mResourceInstanceIdentifier);
    folderSyncScope.filter<Mail::Folder>(QVariant::fromValue(folder.identifier()));
    VERIFYEXEC(Store::synchronize(folderSyncScope));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));


    {
        Sink::Query query;
        query.resourceFilter(mResourceInstanceIdentifier);
        query.request<Mail::Subject>().request<Mail::MimeMessage>().request<Mail::Folder>().request<Mail::Date>();
        query.filter<Mail::Folder>(folder);
        auto mails = Store::read<Mail>(query);
        QCOMPARE(mails.size(), 1);
    }

    resetTestEnvironment();

    createFolder({"uidvalidity"});
    createMessage({"uidvalidity"}, newMessage("new"));

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(folderSyncScope));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

    //Now we should have one message
    auto folder2 = Store::readOne<Folder>(Query{}.resourceFilter(mResourceInstanceIdentifier).filter<Folder::Name>("uidvalidity"));
    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Mail::Subject>().request<Mail::MimeMessage>().request<Mail::Folder>().request<Mail::Date>();
    query.filter<Mail::Folder>(folder2);
    auto mails = Store::read<Mail>(query);
    QCOMPARE(mails.size(), 1);
    QCOMPARE(mails.first().getSubject(), QLatin1String{"new"});
}

