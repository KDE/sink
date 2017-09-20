#include <QtTest>

#include "tests/testutils.h"
#include "../mailtransport.h"

#include "common/test.h"
#include "common/store.h"
#include "common/resourcecontrol.h"
#include "common/domain/applicationdomaintype.h"
#include "common/log.h"
#include "common/secretstore.h"

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
        Sink::SecretStore::instance().insert(resource.identifier(), "doe");
        resource.setProperty("testmode", true);
        return resource;
    }
    QByteArray mResourceInstanceIdentifier;
    QByteArray mStorageResource;

private slots:

    void initTestCase()
    {
        Test::initTest();
        auto resource = createResource();
        QVERIFY(!resource.identifier().isEmpty());
        VERIFYEXEC(Store::create(resource));
        mResourceInstanceIdentifier = resource.identifier();

        auto dummyResource = ApplicationDomain::DummyResource::create("account1");
        VERIFYEXEC(Store::create(dummyResource));
        mStorageResource = dummyResource.identifier();
        QVERIFY(!mStorageResource.isEmpty());
    }

    void cleanup()
    {
        VERIFYEXEC(Store::removeDataFromDisk(mResourceInstanceIdentifier));
        VERIFYEXEC(Store::removeDataFromDisk(mStorageResource));
    }

    void init()
    {
        VERIFYEXEC(ResourceControl::start(mResourceInstanceIdentifier));
    }

    void testSendMail()
    {
        auto message = KMime::Message::Ptr::create();
        message->messageID(true)->generate("foo.com");
        message->subject(true)->fromUnicodeString(QString::fromLatin1("send: Foobar"), "utf8");
        message->assemble();

        auto mail = ApplicationDomain::Mail::create(mResourceInstanceIdentifier);
        mail.setMimeMessage(message->encodedContent(true));

        VERIFYEXEC(Store::create(mail));
        VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

        //FIXME the email is sent already because changereplay kicks of automatically
        //Ensure the mail is queryable in the outbox
        // auto mailInOutbox = Store::readOne<ApplicationDomain::Mail>(Query().resourceFilter(mResourceInstanceIdentifier).filter<Mail::Sent>(false).request<Mail::Subject>().request<Mail::Folder>().request<Mail::MimeMessage>().request<Mail::Sent>());
        // QVERIFY(!mailInOutbox.identifier().isEmpty());

        //Ensure the mail is sent and moved to the sent mail folder on sync
        VERIFYEXEC(Store::synchronize(Query().resourceFilter(mResourceInstanceIdentifier)));
        QTest::qWait(100);
        VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mStorageResource));
        auto mailInSentMailFolder = Store::readOne<ApplicationDomain::Mail>(Query().resourceFilter(mStorageResource).filter<Mail::Sent>(true).request<Mail::Subject>().request<Mail::Folder>().request<Mail::MimeMessage>().request<Mail::Sent>());
        //Check that the mail has been moved to the sent mail folder
        QVERIFY(mailInSentMailFolder.getSent());
        QVERIFY(!mailInSentMailFolder.getSubject().isEmpty());
    }

    void testSendFailure()
    {
        auto message = KMime::Message::Ptr::create();
        message->messageID(true)->generate("foo.com");
        message->subject(true)->fromUnicodeString(QString::fromLatin1("error: Foobar"), "utf8");
        message->assemble();

        auto mail = ApplicationDomain::Mail::create(mResourceInstanceIdentifier);
        mail.setMimeMessage(message->encodedContent(true));

        VERIFYEXEC(Store::create(mail));
        VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

        //Ensure the mail is queryable in the outbox
        auto mailInOutbox = Store::readOne<ApplicationDomain::Mail>(Query().resourceFilter(mResourceInstanceIdentifier).filter<Mail::Sent>(false));
        QVERIFY(!mailInOutbox.identifier().isEmpty());

        //Modify back to drafts
        auto modifiedMail = mailInOutbox;
        modifiedMail.setDraft(true);
        VERIFYEXEC(Store::modify(modifiedMail));
        VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));

        QTest::qWait(100);
        auto mailsInOutbox = Store::read<ApplicationDomain::Mail>(Query().resourceFilter(mResourceInstanceIdentifier));
        QCOMPARE(mailsInOutbox.size(), 0);

        auto mailsInDrafts = Store::read<ApplicationDomain::Mail>(Query().resourceFilter(mStorageResource));
        QCOMPARE(mailsInDrafts.size(), 1);

    }

};


QTEST_MAIN(MailtransportTest)

#include "mailtransporttest.moc"
