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
#include <QtTest>
#include <QTcpSocket>

#include <tests/mailsynctest.h>
#include "../imapresource.h"
#include "../imapserverproxy.h"

#include "common/test.h"
#include "common/domain/applicationdomaintype.h"
#include "common/secretstore.h"
#include "common/store.h"
#include "common/resourcecontrol.h"
#include "common/notifier.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

/**
 * Test of complete system using the imap resource.
 *
 * This test requires the imap resource installed.
 */
class ImapMailSyncTest : public Sink::MailSyncTest
{
    Q_OBJECT

protected:
    bool isBackendAvailable() Q_DECL_OVERRIDE
    {
        QTcpSocket socket;
        socket.connectToHost("localhost", 143);
        return socket.waitForConnected(200);
    }

    void resetTestEnvironment() Q_DECL_OVERRIDE
    {
        system("resetmailbox.sh");
    }

    Sink::ApplicationDomain::SinkResource createResource() Q_DECL_OVERRIDE
    {
        auto resource = ApplicationDomain::ImapResource::create("account1");
        resource.setProperty("server", "localhost");
        resource.setProperty("port", 143);
        resource.setProperty("username", "doe");
        resource.setProperty("daysToSync", 0);
        Sink::SecretStore::instance().insert(resource.identifier(), "doe");
        return resource;
    }

    Sink::ApplicationDomain::SinkResource createFaultyResource() Q_DECL_OVERRIDE
    {
        auto resource = ApplicationDomain::ImapResource::create("account1");
        //Using a bogus ip instead of a bogus hostname avoids getting stuck in the hostname lookup
        resource.setProperty("server", "111.111.1.1");
        resource.setProperty("port", 143);
        resource.setProperty("username", "doe");
        Sink::SecretStore::instance().insert(resource.identifier(), "doe");
        return resource;
    }

    void removeResourceFromDisk(const QByteArray &identifier) Q_DECL_OVERRIDE
    {
        ::ImapResource::removeFromDisk(identifier);
    }

    void createFolder(const QStringList &folderPath) Q_DECL_OVERRIDE
    {
        Imap::ImapServerProxy imap("localhost", 143, Imap::NoEncryption);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.create("INBOX." + folderPath.join('.')));
    }

    void removeFolder(const QStringList &folderPath) Q_DECL_OVERRIDE
    {
        Imap::ImapServerProxy imap("localhost", 143, Imap::NoEncryption);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.remove("INBOX." + folderPath.join('.')));
    }

    QByteArray createMessage(const QStringList &folderPath, const QByteArray &message) Q_DECL_OVERRIDE
    {
        Imap::ImapServerProxy imap("localhost", 143, Imap::NoEncryption);
        VERIFYEXEC_RET(imap.login("doe", "doe"), {});
        VERIFYEXEC_RET(imap.append("INBOX." + folderPath.join('.'), message), {});
        return "2:*";
    }

    void removeMessage(const QStringList &folderPath, const QByteArray &messages) Q_DECL_OVERRIDE
    {
        Imap::ImapServerProxy imap("localhost", 143, Imap::NoEncryption);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.remove("INBOX." + folderPath.join('.'), messages));
    }

    void markAsImportant(const QStringList &folderPath, const QByteArray &messageIdentifier) Q_DECL_OVERRIDE
    {
        Imap::ImapServerProxy imap("localhost", 143, Imap::NoEncryption);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.select("INBOX." + folderPath.join('.')));
        VERIFYEXEC(imap.addFlags(KIMAP2::ImapSet::fromImapSequenceSet(messageIdentifier), QByteArrayList() << Imap::Flags::Flagged));
    }

    static QByteArray newMessage(const QString &subject)
    {
        auto msg = KMime::Message::Ptr::create();
        msg->subject(true)->fromUnicodeString(subject, "utf8");
        msg->date(true)->setDateTime(QDateTime::currentDateTimeUtc());
        msg->assemble();
        return msg->encodedContent(true);
    }

private slots:
    void testNewMailNotification()
    {
        const auto syncFolders = Sink::SyncScope{ApplicationDomain::getTypeName<Folder>()}.resourceFilter(mResourceInstanceIdentifier);
        //Fetch folders initially
        VERIFYEXEC(Store::synchronize(syncFolders));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto folder = Store::readOne<Folder>(Sink::Query{}.resourceFilter(mResourceInstanceIdentifier).filter<Folder::Name>("test"));
        Q_ASSERT(!folder.identifier().isEmpty());

        const auto syncTestMails = Sink::SyncScope{ApplicationDomain::getTypeName<Mail>()}.resourceFilter(mResourceInstanceIdentifier).filter<Mail::Folder>(QVariant::fromValue(folder.identifier()));

        bool notificationReceived = false;
        auto notifier = QSharedPointer<Sink::Notifier>::create(mResourceInstanceIdentifier);
        notifier->registerHandler([&](const Notification &notification) {
            if (notification.type == Sink::Notification::Info && notification.code == ApplicationDomain::NewContentAvailable && notification.entities.contains(folder.identifier())) {
                notificationReceived = true;
            }
        });

        //Should result in a change notification for test
        VERIFYEXEC(Store::synchronize(syncFolders));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        QTRY_VERIFY(notificationReceived);

        notificationReceived = false;

        //Fetch test mails to skip change notification
        VERIFYEXEC(Store::synchronize(syncTestMails));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        //Should no longer result in change notifications for test
        VERIFYEXEC(Store::synchronize(syncFolders));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        QVERIFY(!notificationReceived);

        //Create message and retry
        createMessage(QStringList() << "test", newMessage("This is a Subject."));

        //Should result in change notification
        VERIFYEXEC(Store::synchronize(syncFolders));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        QTRY_VERIFY(notificationReceived);
    }
};

QTEST_MAIN(ImapMailSyncTest)

#include "imapmailsynctest.moc"
