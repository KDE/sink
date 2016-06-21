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
    mCapabilities = resource.getProperty("capabilities").value<QByteArrayList>();
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
    int baseCount = 0;
    //First figure out how many folders we have by default
    {
        auto job = Store::fetchAll<Folder>(Query())
            .then<void, QList<Folder::Ptr>>([&](const QList<Folder::Ptr> &folders) {
                baseCount = folders.size();
            });
        VERIFYEXEC(job);
    }

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
                QCOMPARE(folders.size(), baseCount + 1);
                QHash<QString, Folder::Ptr> foldersByName;
                for (const auto &folder : folders) {
                    foldersByName.insert(folder->getName(), folder);
                }
                QVERIFY(foldersByName.contains(name));
                auto folder = *foldersByName.value(name);
                QCOMPARE(folder.getName(), name);
                QCOMPARE(folder.getIcon(), icon);
            });
        VERIFYEXEC(job);
    }
    VERIFYEXEC(ResourceControl::flushReplayQueue(QByteArrayList() << mResourceInstanceIdentifier));
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::ExistenceInspection(folder, true)));

    if (!mCapabilities.contains("-folder.rename")) {
        QString name2 = "name2";
        QByteArray icon2 = "icon2";
        folder.setName(name2);
        folder.setIcon(icon2);

        VERIFYEXEC(Store::modify(folder));
        VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
        {
            auto job = Store::fetchAll<Folder>(Query::RequestedProperties(QByteArrayList() << Folder::Name::name << Folder::Icon::name))
                .then<void, QList<Folder::Ptr>>([=](const QList<Folder::Ptr> &folders) {
                    QCOMPARE(folders.size(), baseCount + 1);
                    QHash<QString, Folder::Ptr> foldersByName;
                    for (const auto &folder : folders) {
                        foldersByName.insert(folder->getName(), folder);
                    }
                    QVERIFY(foldersByName.contains(name2));
                    auto folder = *foldersByName.value(name2);
                    QCOMPARE(folder.getName(), name2);
                    QCOMPARE(folder.getIcon(), icon2);
                });
            VERIFYEXEC(job);
        }
        VERIFYEXEC(ResourceControl::flushReplayQueue(QByteArrayList() << mResourceInstanceIdentifier));
        VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::ExistenceInspection(folder, true)));
    }

    VERIFYEXEC(Store::remove(folder));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
    {
        auto job = Store::fetchAll<Folder>(Query::RequestedProperties(QByteArrayList() << Folder::Name::name << Folder::Icon::name))
            .then<void, QList<Folder::Ptr>>([=](const QList<Folder::Ptr> &folders) {
                QCOMPARE(folders.size(), baseCount);
            });
        VERIFYEXEC(job);
    }
    VERIFYEXEC(ResourceControl::flushReplayQueue(QByteArrayList() << mResourceInstanceIdentifier));
    //This is not currently possible to check. The local folder and mapping has already been removed.
    // VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::ExistenceInspection(folder, false)));
}

void MailTest::testCreateModifyDeleteMail()
{
    const auto subject = QString::fromLatin1("Foobar");

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
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
    {
        auto job = Store::fetchAll<Mail>(Query::RequestedProperties(QByteArrayList() << Mail::Folder::name << Mail::Subject::name << Mail::MimeMessage::name))
            .then<void, QList<Mail::Ptr>>([=](const QList<Mail::Ptr> &mails) {
                QCOMPARE(mails.size(), 1);
                auto mail = *mails.first();
                QCOMPARE(mail.getSubject(), subject);
                QCOMPARE(mail.getFolder(), folder.identifier());
                QVERIFY(QFile(mail.getMimeMessagePath()).exists());
                KMime::Message m;
                m.setContent(mail.getMimeMessage());
                m.parse();
                QCOMPARE(m.subject(true)->asUnicodeString(), subject);
            });
        VERIFYEXEC(job);
    }

    VERIFYEXEC(ResourceControl::flushReplayQueue(QByteArrayList() << mResourceInstanceIdentifier));
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::ExistenceInspection(mail, true)));
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::PropertyInspection(mail, Mail::Subject::name, subject)));
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::CacheIntegrityInspection(folder)));

    const auto subject2 = QString::fromLatin1("Foobar2");
    auto message2 = KMime::Message::Ptr::create();
    message2->subject(true)->fromUnicodeString(subject2, "utf8");
    message2->assemble();
    mail.setMimeMessage(message2->encodedContent());

    VERIFYEXEC(Store::modify(mail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
    {
        auto job = Store::fetchAll<Mail>(Query::RequestedProperties(QByteArrayList() << Mail::Folder::name << Mail::Subject::name << Mail::MimeMessage::name))
            .then<void, QList<Mail::Ptr>>([=](const QList<Mail::Ptr> &mails) {
                QCOMPARE(mails.size(), 1);
                auto mail = *mails.first();
                QCOMPARE(mail.getSubject(), subject2);
                QCOMPARE(mail.getFolder(), folder.identifier());
                QVERIFY(QFile(mail.getMimeMessagePath()).exists());
                KMime::Message m;
                m.setContent(mail.getMimeMessage());
                m.parse();
                QCOMPARE(m.subject(true)->asUnicodeString(), subject2);
            });
        VERIFYEXEC(job);
    }
    VERIFYEXEC(ResourceControl::flushReplayQueue(QByteArrayList() << mResourceInstanceIdentifier));
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::ExistenceInspection(mail, true)));
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::PropertyInspection(mail, Mail::Subject::name, subject2)));
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::CacheIntegrityInspection(folder)));

    VERIFYEXEC(Store::remove(mail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
    {
        auto job = Store::fetchAll<Mail>(Query::RequestedProperties(QByteArrayList() << Mail::Folder::name << Mail::Subject::name))
            .then<void, QList<Mail::Ptr>>([=](const QList<Mail::Ptr> &mails) {
                QCOMPARE(mails.size(), 0);
            });
        VERIFYEXEC(job);
    }
    VERIFYEXEC(ResourceControl::flushReplayQueue(QByteArrayList() << mResourceInstanceIdentifier));
    // VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::ExistenceInspection(mail, false)));
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::CacheIntegrityInspection(folder)));
}

void MailTest::testMoveMail()
{
    const auto subject = QString::fromLatin1("Foobar");

    auto folder = Folder::create(mResourceInstanceIdentifier);
    folder.setName("folder2");
    VERIFYEXEC(Store::create(folder));

    auto folder1 = Folder::create(mResourceInstanceIdentifier);
    folder1.setName("folder3");
    VERIFYEXEC(Store::create(folder1));

    auto message = KMime::Message::Ptr::create();
    message->subject(true)->fromUnicodeString(subject, "utf8");
    message->assemble();

    auto mail = Mail::create(mResourceInstanceIdentifier);
    mail.setMimeMessage(message->encodedContent());
    mail.setFolder(folder);

    VERIFYEXEC(Store::create(mail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    Mail modifiedMail;
    {
        auto job = Store::fetchAll<Mail>(Query::RequestedProperties(QByteArrayList() << Mail::Folder::name << Mail::Subject::name << Mail::MimeMessage::name))
            .then<void, QList<Mail::Ptr>>([=, &modifiedMail](const QList<Mail::Ptr> &mails) {
                QCOMPARE(mails.size(), 1);
                auto mail = *mails.first();
                modifiedMail = mail;
                QCOMPARE(mail.getFolder(), folder.identifier());
                Warning() << "path: " << mail.getMimeMessagePath();
                QVERIFY(QFile(mail.getMimeMessagePath()).exists());
            });
        VERIFYEXEC(job);
    }

    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::CacheIntegrityInspection(folder)));

    modifiedMail.setFolder(folder1);

    VERIFYEXEC(Store::modify(modifiedMail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
    {
        auto job = Store::fetchAll<Mail>(Query::RequestedProperties(QByteArrayList() << Mail::Folder::name << Mail::Subject::name << Mail::MimeMessage::name))
            .then<void, QList<Mail::Ptr>>([=](const QList<Mail::Ptr> &mails) {
                QCOMPARE(mails.size(), 1);
                auto mail = *mails.first();
                QCOMPARE(mail.getFolder(), folder1.identifier());
                QVERIFY(QFile(mail.getMimeMessagePath()).exists());
                Trace() << "Mime message path: " << mail.getMimeMessagePath();
            });
        VERIFYEXEC(job);
    }
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::CacheIntegrityInspection(folder)));
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::CacheIntegrityInspection(folder1)));
}

void MailTest::testMarkMailAsRead()
{
    auto folder = Folder::create(mResourceInstanceIdentifier);
    folder.setName("anotherfolder");
    VERIFYEXEC(Store::create(folder));

    auto message = KMime::Message::Ptr::create();
    message->subject(true)->fromUnicodeString("subject", "utf8");
    message->assemble();

    auto mail = Mail::create(mResourceInstanceIdentifier);
    mail.setMimeMessage(message->encodedContent());
    mail.setFolder(folder);
    mail.setUnread(true);
    VERIFYEXEC(Store::create(mail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto job = Store::fetchAll<Mail>(Query::ResourceFilter(mResourceInstanceIdentifier) +
                                Query::RequestedProperties(QByteArrayList() << Mail::Folder::name
                                                                            << Mail::Subject::name))
        .then<void, KAsync::Job<void>, QList<Mail::Ptr>>([this](const QList<Mail::Ptr> &mails) {
            ASYNCCOMPARE(mails.size(), 1);
            auto mail = mails.first();
            mail->setUnread(false);
            return Store::modify(*mail)
                .then<void>(ResourceControl::flushReplayQueue(QByteArrayList() << mResourceInstanceIdentifier)) // The change needs to be replayed already
                .then(ResourceControl::inspect<Mail>(ResourceControl::Inspection::PropertyInspection(*mail, Mail::Unread::name, false)))
                .then(ResourceControl::inspect<Mail>(ResourceControl::Inspection::PropertyInspection(*mail, Mail::Subject::name, mail->getSubject())));
        });
    VERIFYEXEC(job);

    // Verify that we can still query for all relevant information
    auto job2 = Store::fetchAll<Mail>(
                        Query::ResourceFilter(mResourceInstanceIdentifier) + Query::RequestedProperties(QByteArrayList() << Mail::Folder::name
                                                                                                                         << Mail::Subject::name
                                                                                                                         << Mail::MimeMessage::name
                                                                                                                         << Mail::Unread::name))
        .then<void, KAsync::Job<void>, QList<Mail::Ptr>>([](const QList<Mail::Ptr> &mails) {
            ASYNCCOMPARE(mails.size(), 1);
            auto mail = mails.first();
            ASYNCVERIFY(!mail->getSubject().isEmpty());
            ASYNCCOMPARE(mail->getUnread(), false);
            ASYNCVERIFY(QFileInfo(mail->getMimeMessagePath()).exists());
            return KAsync::null<void>();
        });
    VERIFYEXEC(job2);
}

void MailTest::testCreateDraft()
{
    if (!mCapabilities.contains(ResourceCapabilities::Mail::drafts)) {
        QSKIP("Resource doesn't have the drafts capability");
    }

    auto message = KMime::Message::Ptr::create();
    message->subject(true)->fromUnicodeString(QString::fromLatin1("Foobar"), "utf8");
    message->assemble();

    auto mail = ApplicationDomain::Mail::create(mResourceInstanceIdentifier);
    mail.setMimeMessage(message->encodedContent());
    mail.setDraft(true);

    VERIFYEXEC(Store::create(mail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    QByteArray folderIdentifier;

    auto createdDraft = Store::readOne<ApplicationDomain::Mail>(Query::IdentityFilter(mail).request<Mail::Folder>());
    folderIdentifier = createdDraft.getFolder();
    QVERIFY(!folderIdentifier.isEmpty());

    //Ensure we can also query by folder
    {
        auto mails = Store::read<ApplicationDomain::Mail>(Query().filter<Mail::Folder>(folderIdentifier));
        bool found = false;
        for (const auto m : mails) {
            if (m.identifier() == mail.identifier()) {
                found = true;
            }
        }
        QVERIFY(found);
    }

    //Ensure the folder is also existing
    {
        ApplicationDomain::Folder folder;
        auto folders = Store::read<ApplicationDomain::Folder>(Query::IdentityFilter(folderIdentifier));
        QCOMPARE(folders.size(), 1);
        folder = folders.first();
        VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::ExistenceInspection(folder, true)));
    }
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::ExistenceInspection(mail, true)));
}

void MailTest::testModifyMailToDraft()
{
    if (!mCapabilities.contains(ResourceCapabilities::Mail::drafts)) {
        QSKIP("Resource doesn't have the drafts capability");
    }

    auto folder = Folder::create(mResourceInstanceIdentifier);
    folder.setName("sdljldskjf");
    VERIFYEXEC(Store::create(folder));

    auto message = KMime::Message::Ptr::create();
    message->subject(true)->fromUnicodeString(QString::fromLatin1("Foobar"), "utf8");
    message->assemble();

    auto mail = ApplicationDomain::Mail::create(mResourceInstanceIdentifier);
    mail.setMimeMessage(message->encodedContent());
    mail.setDraft(false);
    mail.setFolder(folder);

    VERIFYEXEC(Store::create(mail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto modifiedMail = Store::readOne<ApplicationDomain::Mail>(Query::IdentityFilter(mail));
    modifiedMail.setDraft(true);
    VERIFYEXEC(Store::modify(modifiedMail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
    VERIFYEXEC(ResourceControl::flushReplayQueue(QByteArrayList() << mResourceInstanceIdentifier));

    QByteArray folderIdentifier;
    {
        auto createdDraft = Store::readOne<ApplicationDomain::Mail>(Query::IdentityFilter(mail).request<Mail::Folder>());
        folderIdentifier = createdDraft.getFolder();
        QVERIFY(!folderIdentifier.isEmpty());
    }

    //Ensure the folder is also existing
    {
        ApplicationDomain::Folder folder;
        auto folders = Store::read<ApplicationDomain::Folder>(Query::IdentityFilter(folderIdentifier).request<Folder::SpecialPurpose>());
        QCOMPARE(folders.size(), 1);
        folder = folders.first();
        QVERIFY(folder.getSpecialPurpose().contains("drafts"));
        VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::ExistenceInspection(folder, true)));
    }
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::ExistenceInspection(mail, true)));
}

void MailTest::testModifyMailToTrash()
{
    if (!mCapabilities.contains(ResourceCapabilities::Mail::trash)) {
        QSKIP("Resource doesn't have the trash capability");
    }

    auto folder = Folder::create(mResourceInstanceIdentifier);
    folder.setName("sdljldskjf2");
    VERIFYEXEC(Store::create(folder));

    auto message = KMime::Message::Ptr::create();
    message->subject(true)->fromUnicodeString(QString::fromLatin1("Foobar"), "utf8");
    message->assemble();

    auto mail = ApplicationDomain::Mail::create(mResourceInstanceIdentifier);
    mail.setMimeMessage(message->encodedContent());
    mail.setTrash(false);
    mail.setFolder(folder);

    VERIFYEXEC(Store::create(mail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

    auto modifiedMail = Store::readOne<ApplicationDomain::Mail>(Query::IdentityFilter(mail));
    modifiedMail.setTrash(true);
    VERIFYEXEC(Store::modify(modifiedMail));
    VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
    VERIFYEXEC(ResourceControl::flushReplayQueue(QByteArrayList() << mResourceInstanceIdentifier));

    QByteArray folderIdentifier;
    {
        auto createdMail = Store::readOne<ApplicationDomain::Mail>(Query::IdentityFilter(mail).request<Mail::Folder>());
        folderIdentifier = createdMail.getFolder();
        QVERIFY(!folderIdentifier.isEmpty());
    }

    //Ensure the folder is also existing
    {
        ApplicationDomain::Folder folder;
        auto folders = Store::read<ApplicationDomain::Folder>(Query::IdentityFilter(folderIdentifier).request<Folder::SpecialPurpose>());
        QCOMPARE(folders.size(), 1);
        folder = folders.first();
        QVERIFY(folder.getSpecialPurpose().contains("trash"));
        VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::ExistenceInspection(folder, true)));
    }
    VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::ExistenceInspection(mail, true)));
}

#include "mailtest.moc"
