
#include <QtTest>


#include "common/resourcecontrol.h"
#include "common/secretstore.h"
#include "common/store.h"
#include "common/test.h"
#include "tests/testutils.h"
#include <KDAV2/DavItem>
#include <KDAV2/DavUrl>
#include <KDAV2/DavItemCreateJob>
#include <KDAV2/DavCollectionsFetchJob>
#include <KDAV2/DavCollection>


using Sink::ApplicationDomain::Calendar;
using Sink::ApplicationDomain::Event;
using Sink::ApplicationDomain::SinkResource;

class CardDavTest : public QObject
{
    Q_OBJECT

    SinkResource createResource()
    {
        auto resource = Sink::ApplicationDomain::CardDavResource::create("account1");
        resource.setProperty("server", "http://localhost/dav/addressbooks/user/doe");
        resource.setProperty("username", "doe");
        Sink::SecretStore::instance().insert(resource.identifier(), "doe");
        resource.setProperty("testmode", true);
        return resource;
    }


    QByteArray mResourceInstanceIdentifier;

    void createContact(const QString &firstname, const QString &lastname)
    {
        QUrl mainUrl(QStringLiteral("http://localhost/dav/addressbooks/user/doe"));
        mainUrl.setUserName(QStringLiteral("doe"));
        mainUrl.setPassword(QStringLiteral("doe"));


        KDAV2::DavUrl davUrl(mainUrl, KDAV2::CardDav);

        auto *job = new KDAV2::DavCollectionsFetchJob(davUrl);
        job->exec();

        const auto collectionUrl = [&] {
            for(const auto collection : job->collections()) {
                return collection.url().url();
            }
            return QUrl{};
        }();


        QUrl url{collectionUrl.toString() + firstname + lastname + ".vcf"};
        url.setUserInfo(mainUrl.userInfo());
        KDAV2::DavUrl testItemUrl(url, KDAV2::CardDav);
        QByteArray data = QString("BEGIN:VCARD\r\nVERSION:3.0\r\nPRODID:-//Kolab//iRony DAV Server 0.3.1//Sabre//Sabre VObject 2.1.7//EN\r\nUID:12345678-1234-1234-%1-%2\r\nFN:%1 %2\r\nN:%2;%1;;;\r\nEMAIL;TYPE=INTERNET;TYPE=HOME:%1.%2@example.com\r\nREV;VALUE=DATE-TIME:20161221T145611Z\r\nEND:VCARD\r\n").arg(firstname).arg(lastname).toUtf8();
        KDAV2::DavItem item(testItemUrl, QStringLiteral("text/vcard"), data, QString());
        auto createJob = new KDAV2::DavItemCreateJob(item);
        createJob->exec();
        if (createJob->error()) {
            qWarning() << createJob->errorString();
        }
    }

private slots:

    void initTestCase()
    {
        Sink::Test::initTest();
        auto resource = createResource();
        QVERIFY(!resource.identifier().isEmpty());
        VERIFYEXEC(Sink::Store::create(resource));
        mResourceInstanceIdentifier = resource.identifier();
    }

    void cleanup()
    {
        VERIFYEXEC(Sink::Store::removeDataFromDisk(mResourceInstanceIdentifier));
    }

    void init()
    {
        VERIFYEXEC(Sink::ResourceControl::start(mResourceInstanceIdentifier));
    }

    void testSyncContacts()
    {
        createContact("john", "doe");
        createContact("jane", "doe");
        Sink::SyncScope scope;
        scope.setType<Sink::ApplicationDomain::Contact>();
        scope.resourceFilter(mResourceInstanceIdentifier);

        VERIFYEXEC(Sink::Store::synchronize(scope));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        const auto contacts = Sink::Store::read<Sink::ApplicationDomain::Contact>(Sink::Query().resourceFilter(mResourceInstanceIdentifier));
        QCOMPARE(contacts.size(), 2);

        //Ensure a resync works
        {
            VERIFYEXEC(Sink::Store::synchronize(scope));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
            const auto contacts = Sink::Store::read<Sink::ApplicationDomain::Contact>(Sink::Query().resourceFilter(mResourceInstanceIdentifier));
            QCOMPARE(contacts.size(), 2);
        }
    }
};

QTEST_MAIN(CardDavTest)

#include "carddavtest.moc"
