#include <QTest>

#include <tests/mailtest.h>

#include "common/test.h"
#include "common/domain/applicationdomaintype.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

class DummyMailTest : public Sink::MailTest
{
    Q_OBJECT

protected:
    void resetTestEnvironment() Q_DECL_OVERRIDE
    {
    }

    Sink::ApplicationDomain::SinkResource createResource() Q_DECL_OVERRIDE
    {
        auto resource = ApplicationDomain::DummyResource::create("account1");
        return resource;
    }
};

QTEST_MAIN(DummyMailTest)

#include "dummyresourcemailtest.moc"
