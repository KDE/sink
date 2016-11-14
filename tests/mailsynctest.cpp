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

#include <QtTest>

#include <QString>
#include <KMime/Message>

#include "store.h"
#include "resourcecontrol.h"
#include "log.h"
#include "test.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

SINK_DEBUG_AREA("mailsynctest")

void MailSyncTest::initTestCase()
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
            .syncThen<void, QList<Folder::Ptr>>([&](const QList<Folder::Ptr> &folders) {
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

    auto job = Store::fetchAll<Folder>(query).syncThen<void, QList<Folder::Ptr>>([=](const QList<Folder::Ptr> &folders) {
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
            QVERIFY(specialPurposeFolders.contains("drafts"));
        }
        if (mCapabilities.contains(ResourceCapabilities::Mail::trash)) {
            QVERIFY(names.contains("Trash"));
            names.removeAll("Trash");
            QVERIFY(specialPurposeFolders.contains("trash"));
        }
        QCOMPARE(names.size(), 2);
        QVERIFY(names.contains("INBOX"));
        QVERIFY(names.contains("test"));
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

    auto job = Store::fetchAll<Folder>(query).syncThen<void, QList<Folder::Ptr>>([](const QList<Folder::Ptr> &folders) {
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

    auto job = Store::fetchAll<Folder>(query).syncThen<void, QList<Folder::Ptr>>([](const QList<Folder::Ptr> &folders) {
        QStringList names;
        for (const auto &folder : folders) {
            names << folder->getName();
        }
        QVERIFY(!names.contains("test2"));
    });
    VERIFYEXEC(job);
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

    auto job = Store::fetchAll<Folder>(query).syncThen<void, QList<Folder::Ptr>>([=](const QList<Folder::Ptr> &folders) {
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

    auto job = Store::fetchAll<Folder>(query).syncThen<void, QList<Folder::Ptr>>([](const QList<Folder::Ptr> &folders) {
        QStringList names;
        for (const auto &folder : folders) {
            names << folder->getName();
        }
        QVERIFY(names.contains("sub1"));
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

    auto job = Store::fetchAll<Folder>(query).syncThen<void, QList<Folder::Ptr>>([](const QList<Folder::Ptr> &folders) {
        QStringList names;
        for (const auto &folder : folders) {
            names << folder->getName();
        }
        QVERIFY(!names.contains("sub1"));
    });
    VERIFYEXEC(job);
}

void MailSyncTest::testListMails()
{
    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.request<Mail::Subject>().request<Mail::MimeMessage>().request<Mail::Folder>().request<Mail::Date>();

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto job = Store::fetchAll<Mail>(query).syncThen<void, QList<Mail::Ptr>>([](const QList<Mail::Ptr> &mails) {
        QCOMPARE(mails.size(), 1);
        QVERIFY(mails.first()->getSubject().startsWith(QString("[Nepomuk] Jenkins build is still unstable")));
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

void MailSyncTest::testResyncMails()
{
    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto job = Store::fetchAll<Mail>(query).syncThen<void, QList<Mail::Ptr>>([](const QList<Mail::Ptr> &mails) {
        QCOMPARE(mails.size(), 1);
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

    auto msg = KMime::Message::Ptr::create();
    msg->subject(true)->fromUnicodeString("Foobar", "utf8");
    msg->assemble();
    auto messageIdentifier = createMessage(QStringList() << "test", msg->encodedContent(true));

    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    {
        auto job = Store::fetchAll<Mail>(query).syncThen<void, QList<Mail::Ptr>>([](const QList<Mail::Ptr> &mails) {
            QCOMPARE(mails.size(), 2);
        });
        VERIFYEXEC(job);
    }

    removeMessage(QStringList() << "test", messageIdentifier);

    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    {
        auto job = Store::fetchAll<Mail>(query).syncThen<void, QList<Mail::Ptr>>([](const QList<Mail::Ptr> &mails) {
            QCOMPARE(mails.size(), 1);
        });
        VERIFYEXEC(job);
    }
}

void MailSyncTest::testFlagChange()
{
    Sink::Query query;
    query.resourceFilter(mResourceInstanceIdentifier);
    query.filter<Mail::Important>(true);
    query.request<Mail::Subject>().request<Mail::Important>();

    auto msg = KMime::Message::Ptr::create();
    msg->subject(true)->fromUnicodeString("Foobar", "utf8");
    msg->assemble();
    auto messageIdentifier = createMessage(QStringList() << "test", msg->encodedContent(true));

    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    {
        auto job = Store::fetchAll<Mail>(query).syncThen<void, QList<Mail::Ptr>>([](const QList<Mail::Ptr> &mails) {
            QCOMPARE(mails.size(), 0);
        });
        VERIFYEXEC(job);
    }

    markAsImportant(QStringList() << "test", messageIdentifier);

    // Ensure all local data is processed
    VERIFYEXEC(Store::synchronize(query));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    {
        auto job = Store::fetchAll<Mail>(query).syncThen<void, QList<Mail::Ptr>>([](const QList<Mail::Ptr> &mails) {
            QCOMPARE(mails.size(), 1);
            QVERIFY(mails.first()->getImportant());
        });
        VERIFYEXEC(job);
    }

}

void MailSyncTest::testSyncSingleFolder()
{
    VERIFYEXEC(Store::synchronize(Sink::SyncScope{ApplicationDomain::getTypeName<Folder>()}.resourceFilter(mResourceInstanceIdentifier)));
    VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

    Folder::Ptr folder;
    {
        auto job = Store::fetchAll<Folder>(Sink::Query{}.resourceFilter(mResourceInstanceIdentifier).filter<Folder::Name>("test")).template syncThen<void, QList<Folder::Ptr>>([&](const QList<Folder::Ptr> &folders) {
            QCOMPARE(folders.size(), 1);
            folder = folders.first();
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

void MailSyncTest::testFailingSync()
{
    auto resource = createFaultyResource();
    QVERIFY(!resource.identifier().isEmpty());
    VERIFYEXEC(Store::create(resource));

    Sink::Query query;
    query.resourceFilter(resource.identifier());

    // Ensure sync fails if resource is misconfigured
    auto future = Store::synchronize(query).exec();
    future.waitForFinished();
    QVERIFY(future.errorCode());
}

#include "mailsynctest.moc"
