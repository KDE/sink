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

    void createMessage(const QStringList &folderPath, const QByteArray &message) Q_DECL_OVERRIDE
    {
        Imap::ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.append("INBOX." + folderPath.join('.'), message));
    }

    void removeMessage(const QStringList &folderPath, const QByteArray &messages) Q_DECL_OVERRIDE
    {
        Imap::ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.remove("INBOX." + folderPath.join('.'), "2:*"));
    }
};

QTEST_MAIN(ImapMailSyncTest)

#include "imapmailsynctest.moc"
