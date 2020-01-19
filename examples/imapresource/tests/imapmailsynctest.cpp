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
        //We try to connect on localhost on port 0 because:
        //* Using a bogus ip instead of a bogus hostname avoids getting stuck in the hostname lookup.
        //* Using localhost avoids tcp trying to retransmit packets into nirvana
        //* Using port 0 fails immediately because it's not an existing port.
        //All we really want is something that immediately rejects our connection attempt, and this seems to work.
        resource.setProperty("server", "127.0.0.1");
        resource.setProperty("port", 0);
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
        VERIFYEXEC(imap.subscribe("INBOX." + folderPath.join('.')));
    }

    void removeFolder(const QStringList &folderPath) Q_DECL_OVERRIDE
    {
        Imap::ImapServerProxy imap("localhost", 143, Imap::NoEncryption);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.remove("INBOX." + folderPath.join('.')));
    }

    QByteArray createMessage(const QStringList &folderPath, const QByteArray &message, const QDateTime &internalDate)
    {
        Imap::ImapServerProxy imap("localhost", 143, Imap::NoEncryption);
        VERIFYEXEC_RET(imap.login("doe", "doe"), {});

        auto appendJob = imap.append("INBOX." + folderPath.join('.'), message, {}, internalDate);
        auto future = appendJob.exec();
        future.waitForFinished();
        auto result = future.value();
        return QByteArray::number(result);
    }

    QByteArray createMessage(const QStringList &folderPath, const QByteArray &message) Q_DECL_OVERRIDE
    {
        return createMessage(folderPath, message, {});
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

    static QByteArray newMessage(const QString &subject, const QDateTime &dt = QDateTime::currentDateTimeUtc())
    {
        auto msg = KMime::Message::Ptr::create();
        msg->messageID(true)->generate("test.com");
        msg->subject(true)->fromUnicodeString(subject, "utf8");
        msg->date(true)->setDateTime(dt);
        msg->assemble();
        return msg->encodedContent(true);
    }

private slots:
    void testNewMailNotification()
    {
        createFolder(QStringList() << "testNewMailNotification");
        createMessage(QStringList() << "testNewMailNotification", newMessage("Foobar"));

        const auto syncFolders = Sink::SyncScope{ApplicationDomain::getTypeName<Folder>()}.resourceFilter(mResourceInstanceIdentifier);
        //Fetch folders initially
        VERIFYEXEC(Store::synchronize(syncFolders));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto folder = Store::readOne<Folder>(Sink::Query{}.resourceFilter(mResourceInstanceIdentifier).filter<Folder::Name>("testNewMailNotification"));
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
        createMessage(QStringList() << "testNewMailNotification", newMessage("This is a Subject."));

        //Should result in change notification
        VERIFYEXEC(Store::synchronize(syncFolders));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        QTRY_VERIFY(notificationReceived);
    }

    void testSyncFolderBeforeFetchingNewMessages()
    {
        const auto syncScope = Sink::Query{}.resourceFilter(mResourceInstanceIdentifier);

        createFolder(QStringList() << "test3");

        VERIFYEXEC(Store::synchronize(syncScope));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        createMessage(QStringList() << "test3", newMessage("Foobar"));

        VERIFYEXEC(Store::synchronize(syncScope));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto mailQuery = Sink::Query{}.resourceFilter(mResourceInstanceIdentifier).request<Mail::Subject>().filter<Mail::Folder>(Sink::Query{}.filter<Folder::Name>("test3"));
        QCOMPARE(Store::read<Mail>(mailQuery).size(), 1);
    }

    void testDateFilterSync()
    {
        auto dt = QDateTime{{2019, 04, 20}};

        //Create a folder
        createFolder({"datefilter"});
        {
            Sink::Query query;
            query.setType<ApplicationDomain::Folder>();
            VERIFYEXEC(Store::synchronize(query));
            VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        }

        auto folder = Store::readOne<Folder>(Query{}.resourceFilter(mResourceInstanceIdentifier).filter<Folder::Name>("datefilter"));

        //Create the two messsages with one matching the filter below the other not.
        createMessage({"datefilter"}, newMessage("1", dt.addDays(-4)), dt.addDays(-4));
        createMessage({"datefilter"}, newMessage("2", dt.addDays(-2)), dt.addDays(-2));

        {
            Sink::Query query;
            query.setType<ApplicationDomain::Mail>();
            query.resourceFilter(mResourceInstanceIdentifier);
            query.filter(ApplicationDomain::Mail::Date::name, QVariant::fromValue(dt.addDays(-3).date()));
            query.filter<Mail::Folder>(folder);

            VERIFYEXEC(Store::synchronize(query));
            VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        }

        //For the second message we should have the full payload, for the first only the headers
        {
            Sink::Query query;
            query.resourceFilter(mResourceInstanceIdentifier);
            query.filter<Mail::Folder>(folder);
            query.sort<ApplicationDomain::Mail::Date>();
            auto mails = Store::read<Mail>(query);
            QCOMPARE(mails.size(), 2);
            QCOMPARE(mails.at(0).getFullPayloadAvailable(), true);
            QCOMPARE(mails.at(1).getFullPayloadAvailable(), false);
        }
    }

    /*
     * Ensure that even though we have a date-filter we don't leave any gaps in the maillist.
     */
    void testDateFilterForGaps()
    {
        auto dt = QDateTime{{2019, 04, 20}};

        auto foldername = "datefilter1";
        createFolder({foldername});
        createMessage({foldername}, newMessage("0", dt.addDays(-6)), dt.addDays(-6));

        VERIFYEXEC(Store::synchronize(Sink::SyncScope{}));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto folder = Store::readOne<Folder>(Query{}.resourceFilter(mResourceInstanceIdentifier).filter<Folder::Name>(foldername));

        // We create two messages with one not matching the date filter below, and then ensure we get it nevertheless
        createMessage({foldername}, newMessage("1", dt.addDays(-4)), dt.addDays(-4));
        createMessage({foldername}, newMessage("2", dt.addDays(-2)), dt.addDays(-2));

        {
            Sink::Query query;
            query.setType<ApplicationDomain::Mail>();
            query.resourceFilter(mResourceInstanceIdentifier);
            query.filter(ApplicationDomain::Mail::Date::name, QVariant::fromValue(dt.addDays(-3).date()));
            query.filter<Mail::Folder>(folder);

            VERIFYEXEC(Store::synchronize(query));
            VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        }

        {
            Sink::Query query;
            query.resourceFilter(mResourceInstanceIdentifier);
            query.sort<ApplicationDomain::Mail::Date>();
            query.filter<Mail::Folder>(folder);
            auto mails = Store::read<Mail>(query);
            QCOMPARE(mails.size(), 3);
            QCOMPARE(mails.at(0).getFullPayloadAvailable(), true);
            //We don't strictly have to pull the full payload for an item that is just fetched to ensure we have no missing mails,
            //but we currently do
            QCOMPARE(mails.at(1).getFullPayloadAvailable(), true);
            QCOMPARE(mails.at(2).getFullPayloadAvailable(), true);
        }
    }

    /*
     * * First sync the folder
     * * Then create a message on the server
     * * Then attempt to sync it even though it doens't match the date filter.
     * We expect the message to be fetched with the payload even though it doesn't match the date-filter.
     */
    void testDateFilterAfterInitialSync()
    {
        auto dt = QDateTime{{2019, 04, 20}};

        auto foldername = "datefilter2";
        createFolder({foldername});

        Sink::SyncScope query;
        query.setType<ApplicationDomain::Folder>();
        VERIFYEXEC(Store::synchronize(query));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        auto folder = Store::readOne<Folder>(Query{}.resourceFilter(mResourceInstanceIdentifier).filter<Folder::Name>(foldername));

        {
            Sink::Query query;
            query.setType<ApplicationDomain::Mail>();
            query.resourceFilter(mResourceInstanceIdentifier);
            query.filter<Mail::Folder>(folder);

            VERIFYEXEC(Store::synchronize(query));
            VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        }

        createMessage({foldername}, newMessage("0", dt.addDays(-6)), dt.addDays(-6));
        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        {
            Sink::Query query;
            query.setType<ApplicationDomain::Mail>();
            query.resourceFilter(mResourceInstanceIdentifier);
            query.filter(ApplicationDomain::Mail::Date::name, QVariant::fromValue(dt.addDays(-3).date()));
            query.filter<Mail::Folder>(folder);

            VERIFYEXEC(Store::synchronize(query));
            VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        }

        {
            Sink::Query query;
            query.resourceFilter(mResourceInstanceIdentifier);
            query.sort<ApplicationDomain::Mail::Date>();
            query.filter<Mail::Folder>(folder);
            auto mails = Store::read<Mail>(query);
            QCOMPARE(mails.size(), 1);
            QCOMPARE(mails.at(0).getFullPayloadAvailable(), true);
        }
    }
};

QTEST_MAIN(ImapMailSyncTest)

#include "imapmailsynctest.moc"
