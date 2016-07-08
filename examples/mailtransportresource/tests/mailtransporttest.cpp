#include <QtTest>

#include "tests/testutils.h"
#include "../mailtransport.h"

#include "common/test.h"
#include "common/store.h"
#include "common/resourcecontrol.h"
#include "common/domain/applicationdomaintype.h"
#include "common/log.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

class MailtransportTest : public QObject
{
    Q_OBJECT

    Sink::ApplicationDomain::SinkResource createResource()
    {
        auto resource = ApplicationDomain::MailtransportResource::create("account1");
        resource.setProperty("server", "localhost");
        // resource.setProperty("port", 993);
        resource.setProperty("user", "doe");
        resource.setProperty("password", "doe");
        resource.setProperty("testmode", true);
        return resource;
    }
    QByteArray mResourceInstanceIdentifier;

private slots:

    void initTestCase()
    {
        Test::initTest();
        auto resource = createResource();
        QVERIFY(!resource.identifier().isEmpty());
        VERIFYEXEC(Store::create(resource));
        mResourceInstanceIdentifier = resource.identifier();
    }

    void cleanup()
    {
        // VERIFYEXEC(ResourceControl::shutdown(mResourceInstanceIdentifier));
        // removeResourceFromDisk(mResourceInstanceIdentifier);
    }

    void init()
    {
        // VERIFYEXEC(ResourceControl::start(mResourceInstanceIdentifier));
    }

    void testSendMail()
    {
        auto message = KMime::Message::Ptr::create();
        message->subject(true)->fromUnicodeString(QString::fromLatin1("Foobar"), "utf8");
        message->assemble();

        auto mail = ApplicationDomain::Mail::create(mResourceInstanceIdentifier);
        mail.setMimeMessage(message->encodedContent());

        VERIFYEXEC(Store::create(mail));
        VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
        VERIFYEXEC(Store::synchronize(Query::ResourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(ResourceControl::inspect<ApplicationDomain::Mail>(ResourceControl::Inspection::ExistenceInspection(mail, true)));

        auto sentMail = Store::readOne<ApplicationDomain::Mail>(Query::IdentityFilter(mail).request<Mail::Sent>().request<Mail::Subject>());
        QVERIFY(sentMail.getSent());
        QVERIFY(!sentMail.getSubject().isEmpty());
    }

    //TODO test mail that fails to be sent. add a special header to the mail and have the resource fail sending. Ensure we can modify the mail to fix sending of the message.

};


QTEST_MAIN(MailtransportTest)

#include "mailtransporttest.moc"
