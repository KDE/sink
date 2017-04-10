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
    }

    void cleanup()
    {
        Sink::Storage::DataStore storage(Sink::storageLocation(), resourceInstanceIdentifier);
        storage.removeFromDisk();
    }

    void testCleanup()
    {
    }

    void readAll()
    {
        using namespace Sink;
        ResourceContext resourceContext{resourceInstanceIdentifier.toUtf8(), "dummy", AdaptorFactoryRegistry::instance().getFactories("test")};
        Storage::EntityStore store(resourceContext, {});

        auto mail = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>("res1");
        mail.setExtractedMessageId("messageid");
        mail.setExtractedSubject("boo");

        auto mail2 = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>("res1");
        mail2.setExtractedMessageId("messageid2");
        mail2.setExtractedSubject("foo");

        auto mail3 = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::Mail>("res1");
        mail3.setExtractedMessageId("messageid2");
        mail3.setExtractedSubject("foo");

        store.startTransaction(Storage::DataStore::ReadWrite);
        store.add("mail", mail, false, [] (const ApplicationDomain::ApplicationDomainType &) {});
        store.add("mail", mail2, false, [] (const ApplicationDomain::ApplicationDomainType &) {});
        store.add("mail", mail3, false, [] (const ApplicationDomain::ApplicationDomainType &) {});

        mail.setExtractedSubject("foo");

        store.modify("mail", mail, {}, false, [] (const ApplicationDomain::ApplicationDomainType &, ApplicationDomain::ApplicationDomainType &) {});
        store.remove("mail", mail3.identifier(), false, [] (const ApplicationDomain::ApplicationDomainType &) {});
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
