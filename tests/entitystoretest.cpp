#include <QtTest>

#include <QDebug>
#include <QString>

#include "common/storage/entitystore.h"
#include "common/adaptorfactoryregistry.h"
#include "common/definitions.h"
#include "testimplementations.h"

class EntityStoreTest : public QObject
{
    Q_OBJECT
private:
    QString resourceInstanceIdentifier{"resourceId"};

private slots:
    void initTestCase()
    {
        Sink::AdaptorFactoryRegistry::instance().registerFactory<Sink::ApplicationDomain::Mail, TestMailAdaptorFactory>("test");
        Sink::AdaptorFactoryRegistry::instance().registerFactory<Sink::ApplicationDomain::Event, TestEventAdaptorFactory>("test");
    }

    void cleanup()
    {
        Sink::Storage::DataStore(Sink::storageLocation(), resourceInstanceIdentifier).removeFromDisk();
    }

    void testCleanup()
    {
    }

    void testFullScan()
    {
        using namespace Sink;
        ResourceContext resourceContext{resourceInstanceIdentifier.toUtf8(), "dummy", AdaptorFactoryRegistry::instance().getFactories("test")};
        Storage::EntityStore store(resourceContext, {});

        auto mail = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>("res1");
        mail.setExtractedMessageId("messageid");
        mail.setExtractedSubject("boo");
        /*
         * FIXME This triggers "Error while removing value:  "f" "\n\xAE\xDC\xA8|xH\x92\x95\xCC\r\xA7\xAF\xDB}\x9E" "Error on mdb_del: -30798 MDB_NOTFOUND: No matching key/data pair found" Code:  4 Db:  "resourceIdmail.index.draft"":
         *
         * We don't apply the defaults as we should initially, because we don't go via the flatbuffer file that contains the defaults in the first place. This results in this particular case in the draft flag to be invalid instead of false, and thus we end up trying to modify something different in the index than what we added originally.
         * This is true for both create and remove. In the modify case we then get the correct defaults because we load the latest revision from disk, which is based on the flatbuffers file
         * 
         * We now just use setDraft to initialize the entity and get rid of the message. We would of course have to do this for all indexed properties,
         * but we really have to find a better solution than that.
         */
        mail.setDraft(false);

        auto mail2 = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>("res1");
        mail2.setExtractedMessageId("messageid2");
        mail2.setExtractedSubject("foo");

        auto mail3 = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>("res1");
        mail3.setExtractedMessageId("messageid2");
        mail3.setExtractedSubject("foo");

        store.startTransaction(Storage::DataStore::ReadWrite);
        store.add("mail", mail, false);
        store.add("mail", mail2, false);
        store.add("mail", mail3, false);

        mail.setExtractedSubject("foo");

        store.modify("mail", mail, QByteArrayList{}, false);

        {
            const auto ids = store.fullScan("mail");

            QCOMPARE(ids.size(), 3);
            QVERIFY(ids.contains(Sink::Storage::Identifier::fromDisplayByteArray(mail.identifier())));
            QVERIFY(ids.contains(Sink::Storage::Identifier::fromDisplayByteArray(mail2.identifier())));
            QVERIFY(ids.contains(Sink::Storage::Identifier::fromDisplayByteArray(mail3.identifier())));
        }

        store.remove("mail", mail3, false);
        store.commitTransaction();

        {
            const auto ids = store.fullScan("mail");

            QCOMPARE(ids.size(), 2);
            QVERIFY(ids.contains(Sink::Storage::Identifier::fromDisplayByteArray(mail.identifier())));
            QVERIFY(ids.contains(Sink::Storage::Identifier::fromDisplayByteArray(mail2.identifier())));
        }
    }

    void testExistsAndContains()
    {

        using namespace Sink;
        ResourceContext resourceContext{resourceInstanceIdentifier.toUtf8(), "dummy", AdaptorFactoryRegistry::instance().getFactories("test")};
        Storage::EntityStore store(resourceContext, {});

        auto mail = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>("res1");
        mail.setExtractedMessageId("messageid");
        mail.setExtractedSubject("boo");
        //FIXME see above
        mail.setDraft(false);

        auto mail2 = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>("res1");
        mail2.setExtractedMessageId("messageid2");
        mail2.setExtractedSubject("foo");

        auto mail3 = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>("res1");
        mail3.setExtractedMessageId("messageid2");
        mail3.setExtractedSubject("foo");

        auto event = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Event>("res1");
        event.setExtractedUid("messageid2");
        event.setExtractedSummary("foo");

        store.startTransaction(Storage::DataStore::ReadWrite);
        store.add("mail", mail, false);
        store.add("mail", mail2, false);
        store.add("mail", mail3, false);
        store.add("event", event, false);

        mail.setExtractedSubject("foo");

        store.modify("mail", mail, QByteArrayList{}, false);
        store.remove("mail", mail3, false);
        store.commitTransaction();

        QVERIFY(store.contains("mail", mail.identifier()));
        QVERIFY(store.contains("mail", mail2.identifier()));
        QVERIFY(store.contains("mail", mail3.identifier()));
        QVERIFY(store.contains("event", event.identifier()));

        QVERIFY(store.exists("mail", mail.identifier()));
        QVERIFY(store.exists("mail", mail2.identifier()));
        QVERIFY(!store.exists("mail", mail3.identifier()));
        QVERIFY(store.exists("event", event.identifier()));
    }

    void readAll()
    {
        using namespace Sink;
        ResourceContext resourceContext{resourceInstanceIdentifier.toUtf8(), "dummy", AdaptorFactoryRegistry::instance().getFactories("test")};
        Storage::EntityStore store(resourceContext, {});

        auto mail = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>("res1");
        mail.setExtractedMessageId("messageid");
        mail.setExtractedSubject("boo");
        //FIXME see above
        mail.setDraft(false);

        auto mail2 = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>("res1");
        mail2.setExtractedMessageId("messageid2");
        mail2.setExtractedSubject("foo");

        auto mail3 = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>("res1");
        mail3.setExtractedMessageId("messageid2");
        mail3.setExtractedSubject("foo");

        store.startTransaction(Storage::DataStore::ReadWrite);
        store.add("mail", mail, false);
        store.add("mail", mail2, false);
        store.add("mail", mail3, false);

        mail.setExtractedSubject("foo");

        store.modify("mail", mail, QByteArrayList{}, false);
        store.remove("mail", mail3, false);
        store.commitTransaction();

        store.startTransaction(Storage::DataStore::ReadOnly);
        {
            //We get every uid once
            QList<QByteArray> uids;
            store.readAllUids("mail", [&] (const QByteArray &uid) {
                uids << uid;
            });
            QCOMPARE(uids.size(), 2);
        }

        {
            //We get the latest version of every entity once
            QList<QByteArray> uids;
            store.readAll("mail", [&] (const ApplicationDomain::ApplicationDomainType &entity) {
                //The first revision should be superseeded by the modification
                QCOMPARE(entity.getProperty(ApplicationDomain::Mail::Subject::name).toString(), QString::fromLatin1("foo"));
                uids << entity.identifier();
            });
            QCOMPARE(uids.size(), 2);
        }

        store.abortTransaction();

    }
};

QTEST_MAIN(EntityStoreTest)
#include "entitystoretest.moc"
