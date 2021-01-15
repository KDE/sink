#include <QTest>

#include <QDebug>
#include <QString>

#include "common/storage/entitystore.h"
#include "common/datastorequery.h"
#include "common/adaptorfactoryregistry.h"
#include "common/definitions.h"
#include "testimplementations.h"

class DataStoreQueryTest : public QObject
{
    Q_OBJECT
private:
    QString resourceInstanceIdentifier{"resourceId"};

    struct Result {
        QVector<QByteArray> creations;
        QVector<QByteArray> modifications;
        QVector<QByteArray> removals;
    };

    Result readResult (ResultSet &resultSet) {
        Result result;
        resultSet.replaySet(0, 0, [&](const ResultSet::Result &r) {
            switch (r.operation) {
                case Sink::Operation_Creation:
                    result.creations << r.entity.identifier();
                    break;
                case Sink::Operation_Modification:
                    result.modifications << r.entity.identifier();
                    break;
                case Sink::Operation_Removal:
                    result.removals << r.entity.identifier();
                    break;
            }
        });
        return result;
    };


private slots:
    void initTestCase()
    {
        Sink::AdaptorFactoryRegistry::instance().registerFactory<Sink::ApplicationDomain::Mail, TestMailAdaptorFactory>("test");
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

        {
            auto query = DataStoreQuery {{}, "mail", store};
            auto resultset = query.execute();
            const auto result = readResult(resultset);
            QCOMPARE(result.creations.size(), 3);
        }
        //Ensure an incremental query with no changes also yields nothing
        {
            auto query = DataStoreQuery {{}, "mail", store};
            auto resultset = query.update(store.maxRevision() + 1);
            const auto result = readResult(resultset);
            QCOMPARE(result.creations.size(), 0);
            QCOMPARE(result.modifications.size(), 0);
        }

        auto revisionBeforeModification = store.maxRevision();

        mail.setExtractedSubject("foo");
        store.modify("mail", mail, QByteArrayList{}, false);

        {
            auto query = DataStoreQuery {{}, "mail", store};
            auto resultset = query.execute();
            const auto result = readResult(resultset);
            QCOMPARE(result.creations.size(), 3);
        }

        {
            auto query = DataStoreQuery {{}, "mail", store};
            auto resultset = query.update(revisionBeforeModification);
            const auto result = readResult(resultset);
            QCOMPARE(result.modifications.size(), 1);
        }

        store.remove("mail", mail3, false);

        {
            auto query = DataStoreQuery {{}, "mail", store};
            auto resultset = query.execute();
            const auto result = readResult(resultset);
            QCOMPARE(result.creations.size(), 2);
        }
        {
            auto query = DataStoreQuery {{}, "mail", store};
            auto resultset = query.update(revisionBeforeModification);
            const auto result = readResult(resultset);
            QCOMPARE(result.modifications.size(), 1);
            //FIXME we shouldn't have the same id twice
            QCOMPARE(result.removals.size(), 2);
        }
    }


};

QTEST_MAIN(DataStoreQueryTest)
#include "datastorequerytest.moc"
