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

#include <tests/mailsynctest.h>
#include "../imapresource.h"
#include "../imapserverproxy.h"

#include "common/test.h"
#include "common/domain/applicationdomaintype.h"

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
    void resetTestEnvironment() Q_DECL_OVERRIDE
    {
        system("resetmailbox.sh");
    }

    Sink::ApplicationDomain::SinkResource createResource() Q_DECL_OVERRIDE
    {
        auto resource = ApplicationDomain::ImapResource::create("account1");
        resource.setProperty("server", "localhost");
        resource.setProperty("port", 993);
        resource.setProperty("user", "doe");
        resource.setProperty("password", "doe");
        return resource;
    }

    Sink::ApplicationDomain::SinkResource createFaultyResource() Q_DECL_OVERRIDE
    {
        auto resource = ApplicationDomain::ImapResource::create("account1");
        resource.setProperty("server", "foobar");
        resource.setProperty("port", 993);
        resource.setProperty("user", "doe");
        resource.setProperty("password", "doe");
        return resource;
    }

    void removeResourceFromDisk(const QByteArray &identifier) Q_DECL_OVERRIDE
    {
        ::ImapResource::removeFromDisk(identifier);
    }

    void createFolder(const QStringList &folderPath) Q_DECL_OVERRIDE
    {
        Imap::ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.create("INBOX." + folderPath.join('.')));
    }

    void removeFolder(const QStringList &folderPath) Q_DECL_OVERRIDE
    {
        Imap::ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.remove("INBOX." + folderPath.join('.')));
    }

    QByteArray createMessage(const QStringList &folderPath, const QByteArray &message) Q_DECL_OVERRIDE
    {
        Imap::ImapServerProxy imap("localhost", 993);
        imap.login("doe", "doe").exec().waitForFinished();
        imap.append("INBOX." + folderPath.join('.'), message).exec().waitForFinished();
        return "2:*";
    }

    void removeMessage(const QStringList &folderPath, const QByteArray &messages) Q_DECL_OVERRIDE
    {
        Imap::ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.remove("INBOX." + folderPath.join('.'), messages));
    }
};

QTEST_MAIN(ImapMailSyncTest)

#include "imapmailsynctest.moc"
