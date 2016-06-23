#include <QtTest>

#include <tests/mailtest.h>
#include "../imapresource.h"

#include "common/test.h"
#include "common/domain/applicationdomaintype.h"

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
    void resetTestEnvironment() Q_DECL_OVERRIDE
    {
        system("resetmailbox.sh");
    }

    Sink::ApplicationDomain::SinkResource createResource() Q_DECL_OVERRIDE
    {
        auto resource = ApplicationDomain::ImapResource::create("account1");
        resource.setProperty("server", "localhost");
        resource.setProperty("port", 993);
        resource.setProperty("username", "doe");
        resource.setProperty("password", "doe");
        return resource;
    }

    void removeResourceFromDisk(const QByteArray &identifier) Q_DECL_OVERRIDE
    {
        ::ImapResource::removeFromDisk(identifier);
    }
};

QTEST_MAIN(ImapMailTest)

#include "imapmailtest.moc"
