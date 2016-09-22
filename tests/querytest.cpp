#include <QtTest>

#include <QString>

#include "resource.h"
#include "store.h"
#include "resourcecontrol.h"
#include "commands.h"
#include "resourceconfig.h"
#include "log.h"
#include "modelresult.h"
#include "test.h"
#include "testutils.h"

using namespace Sink::ApplicationDomain;

/**
 * Test of the query system using the dummy resource.
 *
 * This test requires the dummy resource installed.
 */
class QueryTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        Sink::Test::initTest();
        auto factory = Sink::ResourceFactory::load("sink.dummy");
        QVERIFY(factory);
        ResourceConfig::addResource("sink.dummy.instance1", "sink.dummy");
        Sink::Store::removeDataFromDisk(QByteArray("sink.dummy.instance1")).exec().waitForFinished();
    }

    void cleanup()
    {
        Sink::Store::removeDataFromDisk(QByteArray("sink.dummy.instance1")).exec().waitForFinished();
    }

    void init()
    {
        qDebug();
        qDebug() << "-----------------------------------------";
        qDebug();
    }

    void testNoResources()
    {
        // Test
        Sink::Query query;
        query.resources << "foobar";
        query.liveQuery = true;

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 0);
    }


    void testSingle()
    {
        // Setup
        {
            Sink::ApplicationDomain::Mail mail("sink.dummy.instance1");
            Sink::Store::create<Sink::ApplicationDomain::Mail>(mail).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resources << "sink.dummy.instance1";
        query.liveQuery = true;

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_COMPARE(model->rowCount(), 1);
    }

    void testSingleWithDelay()
    {
        // Setup
        {
            Sink::ApplicationDomain::Mail mail("sink.dummy.instance1");
            Sink::Store::create<Sink::ApplicationDomain::Mail>(mail).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resources << "sink.dummy.instance1";
        query.liveQuery = false;

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        // We fetch after the data is available and don't rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);

        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
    }

    void testFilter()
    {
        // Setup
        {
            Mail mail("sink.dummy.instance1");
            mail.setUid("test1");
            mail.setFolder("folder1");
            Sink::Store::create<Mail>(mail).exec().waitForFinished();
        }
        {
            Mail mail("sink.dummy.instance1");
            mail.setUid("test2");
            mail.setFolder("folder2");
            Sink::Store::create<Mail>(mail).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resources << "sink.dummy.instance1";
        query.liveQuery = true;
        query.filter<Mail::Folder>("folder1");

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_COMPARE(model->rowCount(), 1);

        auto mail = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Mail::Ptr>();
        {
            mail->setFolder("folder2");
            Sink::Store::modify<Mail>(*mail).exec().waitForFinished();
        }
        QTRY_COMPARE(model->rowCount(), 0);

        {
            mail->setFolder("folder1");
            Sink::Store::modify<Mail>(*mail).exec().waitForFinished();
        }
        QTRY_COMPARE(model->rowCount(), 1);
    }

    void testById()
    {
        QByteArray id;
        // Setup
        {
            Sink::ApplicationDomain::Mail mail("sink.dummy.instance1");
            Sink::Store::create<Sink::ApplicationDomain::Mail>(mail).exec().waitForFinished();
            Sink::Store::create<Sink::ApplicationDomain::Mail>(mail).exec().waitForFinished();

            Sink::Query query;
            query.resources << "sink.dummy.instance1";

            // Ensure all local data is processed
            Sink::Store::synchronize(query).exec().waitForFinished();

            // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QVERIFY(model->rowCount() >= 1);
            id = model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Mail::Ptr>()->identifier();
        }

        // Test
        Sink::Query query;
        query.resources << "sink.dummy.instance1";
        query.ids << id;
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
    }

    void testFolder()
    {
        // Setup
        {
            Sink::ApplicationDomain::Folder folder("sink.dummy.instance1");
            Sink::Store::create<Sink::ApplicationDomain::Folder>(folder).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resources << "sink.dummy.instance1";
        query.liveQuery = true;

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_COMPARE(model->rowCount(), 1);
        auto folderEntity = model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Folder::Ptr>();
        QVERIFY(!folderEntity->identifier().isEmpty());
    }

    void testFolderTree()
    {
        // Setup
        {
            Sink::ApplicationDomain::Folder folder("sink.dummy.instance1");
            Sink::Store::create<Sink::ApplicationDomain::Folder>(folder).exec().waitForFinished();

            Sink::Query query;
            query.resources << "sink.dummy.instance1";

            // Ensure all local data is processed
            Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);

            auto folderEntity = model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Folder::Ptr>();
            QVERIFY(!folderEntity->identifier().isEmpty());

            Sink::ApplicationDomain::Folder subfolder("sink.dummy.instance1");
            subfolder.setProperty("parent", folderEntity->identifier());
            Sink::Store::create<Sink::ApplicationDomain::Folder>(subfolder).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resources << "sink.dummy.instance1";
        query.parentProperty = "parent";

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        // We fetch after the data is available and don't rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
        model->fetchMore(model->index(0, 0));
        QTRY_VERIFY(model->data(model->index(0, 0), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(model->index(0, 0)), 1);
    }

    void testMailByUid()
    {
        // Setup
        {
            Sink::ApplicationDomain::Mail mail("sink.dummy.instance1");
            mail.setProperty("uid", "test1");
            mail.setProperty("sender", "doe@example.org");
            Sink::Store::create<Sink::ApplicationDomain::Mail>(mail).exec().waitForFinished();
        }

        {
            Sink::ApplicationDomain::Mail mail("sink.dummy.instance1");
            mail.setProperty("uid", "test2");
            mail.setProperty("sender", "doe@example.org");
            Sink::Store::create<Sink::ApplicationDomain::Mail>(mail).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resources << "sink.dummy.instance1";
        query.liveQuery = false;
        query += Sink::Query::PropertyFilter("uid", "test1");

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
    }

    void testMailByFolder()
    {
        // Setup
        Sink::ApplicationDomain::Folder::Ptr folderEntity;
        {
            Sink::ApplicationDomain::Folder folder("sink.dummy.instance1");
            Sink::Store::create<Sink::ApplicationDomain::Folder>(folder).exec().waitForFinished();

            Sink::Query query;
            query.resources << "sink.dummy.instance1";

            // Ensure all local data is processed
            Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);

            folderEntity = model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Folder::Ptr>();
            QVERIFY(!folderEntity->identifier().isEmpty());

            Sink::ApplicationDomain::Mail mail("sink.dummy.instance1");
            mail.setProperty("uid", "test1");
            mail.setProperty("folder", folderEntity->identifier());
            Sink::Store::create<Sink::ApplicationDomain::Mail>(mail).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resources << "sink.dummy.instance1";
        query += Sink::Query::PropertyFilter("folder", *folderEntity);

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
    }

    /*
     * Filter by two properties to make sure that we also use a non-index based filter.
     */
    void testMailByUidAndFolder()
    {
        // Setup
        Folder::Ptr folderEntity;
        {
            Folder folder("sink.dummy.instance1");
            Sink::Store::create<Folder>(folder).exec().waitForFinished();

            Sink::Query query;
            query.resources << "sink.dummy.instance1";

            // Ensure all local data is processed
            Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

            auto model = Sink::Store::loadModel<Folder>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);

            folderEntity = model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Folder::Ptr>();
            QVERIFY(!folderEntity->identifier().isEmpty());

            Mail mail("sink.dummy.instance1");
            mail.setProperty("uid", "test1");
            mail.setProperty("folder", folderEntity->identifier());
            Sink::Store::create<Mail>(mail).exec().waitForFinished();

            Mail mail1("sink.dummy.instance1");
            mail1.setProperty("uid", "test1");
            mail1.setProperty("folder", "foobar");
            Sink::Store::create<Mail>(mail1).exec().waitForFinished();

            Mail mail2("sink.dummy.instance1");
            mail2.setProperty("uid", "test2");
            mail2.setProperty("folder", folderEntity->identifier());
            Sink::Store::create<Mail>(mail2).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resources << "sink.dummy.instance1";
        query.filter<Mail::Folder>(*folderEntity);
        query.filter<Mail::Uid>("test1");

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
    }

    void testMailByFolderSortedByDate()
    {
        // Setup
        Sink::ApplicationDomain::Folder::Ptr folderEntity;
        {
            Sink::ApplicationDomain::Folder folder("sink.dummy.instance1");
            Sink::Store::create<Sink::ApplicationDomain::Folder>(folder).exec().waitForFinished();

            Sink::Query query;
            query.resources << "sink.dummy.instance1";

            // Ensure all local data is processed
            Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);

            folderEntity = model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Folder::Ptr>();
            QVERIFY(!folderEntity->identifier().isEmpty());

            const auto date = QDateTime(QDate(2015, 7, 7), QTime(12, 0));
            {
                Sink::ApplicationDomain::Mail mail("sink.dummy.instance1");
                mail.setProperty("uid", "testSecond");
                mail.setProperty("folder", folderEntity->identifier());
                mail.setProperty("date", date.addDays(-1));
                Sink::Store::create<Sink::ApplicationDomain::Mail>(mail).exec().waitForFinished();
            }
            {
                Sink::ApplicationDomain::Mail mail("sink.dummy.instance1");
                mail.setProperty("uid", "testLatest");
                mail.setProperty("folder", folderEntity->identifier());
                mail.setProperty("date", date);
                Sink::Store::create<Sink::ApplicationDomain::Mail>(mail).exec().waitForFinished();
            }
            {
                Sink::ApplicationDomain::Mail mail("sink.dummy.instance1");
                mail.setProperty("uid", "testLast");
                mail.setProperty("folder", folderEntity->identifier());
                mail.setProperty("date", date.addDays(-2));
                Sink::Store::create<Sink::ApplicationDomain::Mail>(mail).exec().waitForFinished();
            }
        }

        // Test
        Sink::Query query;
        query.resources << "sink.dummy.instance1";
        query += Sink::Query::PropertyFilter("folder", *folderEntity);
        query.sortProperty = "date";
        query.limit = 1;
        query.liveQuery = false;

        // Ensure all local data is processed
        Sink::ResourceControl::flushMessageQueue(query.resources).exec().waitForFinished();

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        // The model is not sorted, but the limited set is sorted, so we can only test for the latest result.
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Mail::Ptr>()->getProperty("uid").toByteArray(), QByteArray("testLatest"));

        model->fetchMore(QModelIndex());
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 2);
        // We can't make any assumptions about the order of the indexes
        // QCOMPARE(model->index(1, 0).data(Sink::Store::DomainObjectRole).value<Sink::ApplicationDomain::Mail::Ptr>()->getProperty("uid").toByteArray(), QByteArray("testSecond"));
    }

    void testReactToNewResource()
    {
        Sink::Query query;
        query.liveQuery = true;
        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Folder>(query);
        QTRY_COMPARE(model->rowCount(QModelIndex()), 0);

        auto res = Sink::ApplicationDomain::DummyResource::create("");
        VERIFYEXEC(Sink::Store::create(res));
        auto folder = Sink::ApplicationDomain::Folder::create(res.identifier());
        VERIFYEXEC(Sink::Store::create(folder));
        QTRY_COMPARE(model->rowCount(QModelIndex()), 1);

        VERIFYEXEC(Sink::Store::remove(res));
    }
};

QTEST_MAIN(QueryTest)
#include "querytest.moc"
