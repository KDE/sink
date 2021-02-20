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
#include <QTest>
#include <QTcpSocket>
#include <KMime/Message>

#include <tests/mailtest.h>

#include "common/test.h"
#include "common/domain/applicationdomaintype.h"
#include "common/secretstore.h"
#include "common/resourcecontrol.h"
#include "common/store.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

/**
 * Test of complete system using the imap resource.
 *
 * This test requires the imap resource installed.
 */
class ImapMailTest : public Sink::MailTest
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

private slots:

    void testBogusMessageAppend()
    {
        using namespace Sink;
        using namespace Sink::ApplicationDomain;

        auto folder = Folder::create(mResourceInstanceIdentifier);
        folder.setName("bogusfolder");
        VERIFYEXEC(Store::create(folder));

        Mail bogusMail;
        {
            auto mail = Mail::create(mResourceInstanceIdentifier);
            mail.setMimeMessage("Bogus message: \0 this doesn't make any sense and contains NUL.");
            mail.setFolder(folder);

            VERIFYEXEC(Store::create(mail));

            VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
            auto mails = Store::read<Mail>(Query().request<Mail::Folder>().request<Mail::Subject>().request<Mail::MimeMessage>());
            QCOMPARE(mails.size(), 1);
            bogusMail = mails.at(0);

            VERIFYEXEC(ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));
            VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::ExistenceInspection(mail, false)));
            //The cache will be off by one (because we failed to replay)
            // VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::CacheIntegrityInspection(folder)));
        }


        //Ensure we can still append further messages:
        {
            auto mail = Mail::create(mResourceInstanceIdentifier);
            {
                auto message = KMime::Message::Ptr::create();
                message->subject(true)->fromUnicodeString("Subject", "utf8");
                message->assemble();
                mail.setMimeMessage(message->encodedContent(true));
            }
            mail.setFolder(folder);
            VERIFYEXEC(Store::create(mail));

            VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
            auto mails = Store::read<Mail>(Query().request<Mail::Folder>().request<Mail::Subject>().request<Mail::MimeMessage>());
            QCOMPARE(mails.size(), 2);

            VERIFYEXEC(ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));
            //The mail is still not available, because we'll end up trying to replay the bogus mail again.
            VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::ExistenceInspection(mail, false)));

            //Fix the situation by deleting the bogus mail and retrying to sync.
            VERIFYEXEC(Store::remove(bogusMail));
            VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
            VERIFYEXEC(ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

            //This will fail because we still try to resync the previous mail
            VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::ExistenceInspection(mail, true)));
            //The cache will be off by one (because we failed to replay)
            VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Folder>(ResourceControl::Inspection::CacheIntegrityInspection(folder)));
        }

    }
};

QTEST_MAIN(ImapMailTest)

#include "imapmailtest.moc"
