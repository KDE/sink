#include <QtTest>

#include <tests/mailtest.h>

#include "common/test.h"
#include "common/domain/applicationdomaintype.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

/**
 * Test of complete system using the maildir resource.
 *
 * This test requires the maildir resource installed.
 */
class MaildirMailTest : public Sink::MailTest
{
    Q_OBJECT

    QTemporaryDir tempDir;
    QString targetPath;

protected:
    void resetTestEnvironment() Q_DECL_OVERRIDE
    {
        targetPath = tempDir.path() + "/maildir1/";
    }

    Sink::ApplicationDomain::SinkResource createResource() Q_DECL_OVERRIDE
    {
        auto resource = ApplicationDomain::MaildirResource::create("account1");
        resource.setProperty("path", targetPath);
        return resource;
    }
};

QTEST_MAIN(MaildirMailTest)

#include "maildirmailtest.moc"
