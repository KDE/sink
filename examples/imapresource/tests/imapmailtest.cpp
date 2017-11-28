#include <QtTest>
#include <QTcpSocket>

#include <tests/mailtest.h>

#include "common/test.h"
#include "common/domain/applicationdomaintype.h"
#include "common/secretstore.h"

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
        Sink::SecretStore::instance().insert(resource.identifier(), "doe");
        return resource;
    }
};

QTEST_MAIN(ImapMailTest)

#include "imapmailtest.moc"
