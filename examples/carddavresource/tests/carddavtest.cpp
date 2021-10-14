
#include <QTest>


#include "common/resourcecontrol.h"
#include "common/secretstore.h"
#include "common/store.h"
#include "common/test.h"
#include "tests/testutils.h"
#include <KDAV2/DavItem>
#include <KDAV2/DavUrl>
#include <KDAV2/DavItemCreateJob>
#include <KDAV2/DavCollectionsFetchJob>
#include <KDAV2/DavCollectionCreateJob>
#include <KDAV2/DavCollection>
#include <KContacts/Addressee>
#include <KContacts/VCardConverter>


using Sink::ApplicationDomain::Addressbook;
using Sink::ApplicationDomain::Contact;
using Sink::ApplicationDomain::SinkResource;

class CardDavTest : public QObject
{
    Q_OBJECT

    SinkResource createResource()
    {
        auto resource = Sink::ApplicationDomain::CardDavResource::create("account1");
        resource.setProperty("server", "http://localhost");
        resource.setProperty("username", "doe");
        Sink::SecretStore::instance().insert(resource.identifier(), "doe");
        return resource;
    }


    QByteArray mResourceInstanceIdentifier;

    void createContact(const QString &firstname, const QString &lastname, const QString &collectionName)
    {
        QUrl mainUrl(QStringLiteral("http://localhost/dav/addressbooks/user/doe"));
        mainUrl.setUserName(QStringLiteral("doe"));
        mainUrl.setPassword(QStringLiteral("doe"));

        KDAV2::DavUrl davUrl(mainUrl, KDAV2::CardDav);

        auto *job = new KDAV2::DavCollectionsFetchJob(davUrl);
        job->exec();

        const auto collectionUrl = [&] {
            for (const auto &col : job->collections()) {
                if (col.displayName() == collectionName) {
                    return col.url().url();
                }
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

    void createCollection(const QString &name)
    {
        QUrl mainUrl(QStringLiteral("http://localhost/dav/addressbooks/user/doe/") + name);
        mainUrl.setUserName(QStringLiteral("doe"));
        mainUrl.setPassword(QStringLiteral("doe"));

        KDAV2::DavUrl davUrl(mainUrl, KDAV2::CardDav);
        KDAV2::DavCollection collection{davUrl, name, KDAV2::DavCollection::Contacts};

        auto createJob = new KDAV2::DavCollectionCreateJob(collection);
        createJob->exec();
        if (createJob->error()) {
            qWarning() << createJob->errorString();
        }
    }

    void resetTestEnvironment()
    {
        system("resetmailbox.sh");
    }

private slots:

    void initTestCase()
    {
        Sink::Test::initTest();
        resetTestEnvironment();
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

    void testSyncAddressbooks()
    {
        createCollection("addressbook2");

        Sink::SyncScope scope;
        scope.setType<Addressbook>();
        scope.resourceFilter(mResourceInstanceIdentifier);

        VERIFYEXEC(Sink::Store::synchronize(scope));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        const auto addressbooks = Sink::Store::read<Addressbook>(Sink::Query().resourceFilter(mResourceInstanceIdentifier));
        QCOMPARE(addressbooks.size(), 2);
    }

    void testSyncContacts()
    {
        createContact("john", "doe", "personal");
        createContact("jane", "doe", "personal");
        createContact("fred", "durst", "addressbook2");
        Sink::SyncScope scope;
        scope.resourceFilter(mResourceInstanceIdentifier);

        VERIFYEXEC(Sink::Store::synchronize(scope));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        const auto contacts = Sink::Store::read<Sink::ApplicationDomain::Contact>(Sink::Query().resourceFilter(mResourceInstanceIdentifier));
        QCOMPARE(contacts.size(), 3);

        //Ensure a resync works
        {
            VERIFYEXEC(Sink::Store::synchronize(scope));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
            const auto contacts = Sink::Store::read<Sink::ApplicationDomain::Contact>(Sink::Query().resourceFilter(mResourceInstanceIdentifier));
            QCOMPARE(contacts.size(), 3);
        }

        //Ensure a resync after another creation works
        createContact("alf", "alf", "addressbook2");
        {
            VERIFYEXEC(Sink::Store::synchronize(scope));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
            const auto contacts = Sink::Store::read<Sink::ApplicationDomain::Contact>(Sink::Query().resourceFilter(mResourceInstanceIdentifier));
            QCOMPARE(contacts.size(), 4);
        }
    }

    void testAddModifyRemoveContact()
    {
        auto createVCard = [] (const QString &firstname, const QString &uid) {
            KContacts::Addressee addressee;
            addressee.setGivenName(firstname);
            addressee.setFamilyName("Doe");
            addressee.setFormattedName("John Doe");
            addressee.setUid(uid);
            return KContacts::VCardConverter{}.createVCard(addressee, KContacts::VCardConverter::v3_0);
        };


        Sink::SyncScope scope;
        scope.setType<Addressbook>();
        scope.resourceFilter(mResourceInstanceIdentifier);

        VERIFYEXEC(Sink::Store::synchronize(scope));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto addressbooks = Sink::Store::read<Addressbook>(Sink::Query().resourceFilter(mResourceInstanceIdentifier));
        QVERIFY(!addressbooks.isEmpty());


        auto addedUid = QUuid::createUuid().toString();
        auto contact = Sink::ApplicationDomain::ApplicationDomainType::createEntity<Sink::ApplicationDomain::Contact>(mResourceInstanceIdentifier);
        contact.setVcard(createVCard("John", addedUid));
        contact.setAddressbook(addressbooks.first());

        {
            VERIFYEXEC(Sink::Store::create(contact));
            VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

            auto contacts = Sink::Store::read<Contact>(Sink::Query().filter("uid", Sink::Query::Comparator(addedUid)));
            QCOMPARE(contacts.size(), 1);
            QCOMPARE(contacts.first().getFirstname(), QLatin1String{"John"});
        }


        {
            contact.setVcard(createVCard("Jane", addedUid));
            VERIFYEXEC(Sink::Store::modify(contact));
            VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));
            VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
            auto contacts = Sink::Store::read<Contact>(Sink::Query().filter("uid", Sink::Query::Comparator(addedUid)));
            QCOMPARE(contacts.size(), 1);
            QCOMPARE(contacts.first().getFirstname(), QLatin1String{"Jane"});
        }

        {
            VERIFYEXEC(Sink::Store::remove(contact));
            VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));
            VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
            auto contacts = Sink::Store::read<Contact>(Sink::Query().filter("uid", Sink::Query::Comparator(addedUid)));
            QCOMPARE(contacts.size(), 0);
        }
    }
};

QTEST_MAIN(CardDavTest)

#include "carddavtest.moc"
