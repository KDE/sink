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

using namespace Sink;
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
        VERIFYEXEC(Sink::Store::removeDataFromDisk(QByteArray("sink.dummy.instance1")));
    }

    void cleanup()
    {
        VERIFYEXEC(Sink::Store::removeDataFromDisk(QByteArray("sink.dummy.instance1")));
    }

    void init()
    {
        qDebug();
        qDebug() << "-----------------------------------------";
        qDebug();
    }

    void testSerialization()
    {

        auto type = QByteArray("type");
        auto sort = QByteArray("sort");

        Sink::QueryBase::Filter filter;
        filter.ids << "id";
        filter.propertyFilter.insert("foo", QVariant::fromValue(QByteArray("bar")));

        Sink::Query query;
        query.setFilter(filter);
        query.setType(type);
        query.setSortProperty(sort);

        QByteArray data;
        {
            QDataStream stream(&data, QIODevice::WriteOnly);
            stream << query;
        }

        Sink::Query deserializedQuery;
        {
            QDataStream stream(&data, QIODevice::ReadOnly);
            stream >> deserializedQuery;
        }

        QCOMPARE(deserializedQuery.type(), type);
        QCOMPARE(deserializedQuery.sortProperty(), sort);
        QCOMPARE(deserializedQuery.getFilter().ids, filter.ids);
        QCOMPARE(deserializedQuery.getFilter().propertyFilter.keys(), filter.propertyFilter.keys());
        QCOMPARE(deserializedQuery.getFilter().propertyFilter, filter.propertyFilter);
    }

    void testNoResources()
    {
        // Test
        Sink::Query query;
        query.resourceFilter("foobar");
        query.setFlags(Query::LiveQuery);

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 0);
    }


    void testSingle()
    {
        // Setup
        {
            Mail mail("sink.dummy.instance1");
            VERIFYEXEC(Sink::Store::create<Mail>(mail));
        }

        // Test
        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");
        query.setFlags(Query::LiveQuery);

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_COMPARE(model->rowCount(), 1);
    }

    void testSingleWithDelay()
    {
        // Setup
        {
            Mail mail("sink.dummy.instance1");
            Sink::Store::create<Mail>(mail).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        // We fetch after the data is available and don't rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Mail>(query);

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
        query.resourceFilter("sink.dummy.instance1");
        query.setFlags(Query::LiveQuery);
        query.filter<Mail::Folder>("folder1");

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_COMPARE(model->rowCount(), 1);

        auto mail = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Mail::Ptr>();
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
            Mail mail("sink.dummy.instance1");
            Sink::Store::create<Mail>(mail).exec().waitForFinished();
            Sink::Store::create<Mail>(mail).exec().waitForFinished();

            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");

            // Ensure all local data is processed
            Sink::Store::synchronize(query).exec().waitForFinished();

            // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
            auto model = Sink::Store::loadModel<Mail>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QVERIFY(model->rowCount() >= 1);
            id = model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Mail::Ptr>()->identifier();
        }

        // Test
        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");
        query.filter(id);
        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
    }

    void testFolder()
    {
        // Setup
        {
            Folder folder("sink.dummy.instance1");
            Sink::Store::create<Folder>(folder).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");
        query.setFlags(Query::LiveQuery);

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Folder>(query);
        QTRY_COMPARE(model->rowCount(), 1);
        auto folderEntity = model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Folder::Ptr>();
        QVERIFY(!folderEntity->identifier().isEmpty());
    }

    void testFolderTree()
    {
        // Setup
        {
            Folder folder("sink.dummy.instance1");
            Sink::Store::create<Folder>(folder).exec().waitForFinished();

            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");

            // Ensure all local data is processed
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

            auto model = Sink::Store::loadModel<Folder>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);

            auto folderEntity = model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Folder::Ptr>();
            QVERIFY(!folderEntity->identifier().isEmpty());

            Folder subfolder("sink.dummy.instance1");
            subfolder.setProperty("parent", folderEntity->identifier());
            Sink::Store::create<Folder>(subfolder).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");
        query.parentProperty = "parent";

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        // We fetch after the data is available and don't rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Folder>(query);
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
            Mail mail("sink.dummy.instance1");
            mail.setProperty("uid", "test1");
            mail.setProperty("sender", "doe@example.org");
            Sink::Store::create<Mail>(mail).exec().waitForFinished();
        }

        {
            Mail mail("sink.dummy.instance1");
            mail.setProperty("uid", "test2");
            mail.setProperty("sender", "doe@example.org");
            Sink::Store::create<Mail>(mail).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");
        query.filter<Mail::Uid>("test1");

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
    }

    void testMailByFolder()
    {
        // Setup
        Folder::Ptr folderEntity;
        {
            Folder folder("sink.dummy.instance1");
            Sink::Store::create<Folder>(folder).exec().waitForFinished();

            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");

            // Ensure all local data is processed
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

            auto model = Sink::Store::loadModel<Folder>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);

            folderEntity = model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Folder::Ptr>();
            QVERIFY(!folderEntity->identifier().isEmpty());

            Mail mail("sink.dummy.instance1");
            mail.setProperty("uid", "test1");
            mail.setProperty("folder", folderEntity->identifier());
            Sink::Store::create<Mail>(mail).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");
        query.filter<Mail::Folder>(*folderEntity);

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Mail>(query);
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
            query.resourceFilter("sink.dummy.instance1");

            // Ensure all local data is processed
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

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
        query.resourceFilter("sink.dummy.instance1");
        query.filter<Mail::Folder>(*folderEntity);
        query.filter<Mail::Uid>("test1");

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
    }

    void testMailByFolderSortedByDate()
    {
        // Setup
        Folder::Ptr folderEntity;
        {
            Folder folder("sink.dummy.instance1");
            Sink::Store::create<Folder>(folder).exec().waitForFinished();

            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");

            // Ensure all local data is processed
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

            auto model = Sink::Store::loadModel<Folder>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);

            folderEntity = model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Folder::Ptr>();
            QVERIFY(!folderEntity->identifier().isEmpty());

            const auto date = QDateTime(QDate(2015, 7, 7), QTime(12, 0));
            {
                Mail mail("sink.dummy.instance1");
                mail.setProperty("uid", "testSecond");
                mail.setProperty("folder", folderEntity->identifier());
                mail.setProperty("date", date.addDays(-1));
                Sink::Store::create<Mail>(mail).exec().waitForFinished();
            }
            {
                Mail mail("sink.dummy.instance1");
                mail.setProperty("uid", "testLatest");
                mail.setProperty("folder", folderEntity->identifier());
                mail.setProperty("date", date);
                Sink::Store::create<Mail>(mail).exec().waitForFinished();
            }
            {
                Mail mail("sink.dummy.instance1");
                mail.setProperty("uid", "testLast");
                mail.setProperty("folder", folderEntity->identifier());
                mail.setProperty("date", date.addDays(-2));
                Sink::Store::create<Mail>(mail).exec().waitForFinished();
            }
        }

        // Test
        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");
        query.filter<Mail::Folder>(*folderEntity);
        query.sort<Mail::Date>();
        query.limit(1);

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        // The model is not sorted, but the limited set is sorted, so we can only test for the latest result.
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Mail::Ptr>()->getProperty("uid").toByteArray(), QByteArray("testLatest"));

        model->fetchMore(QModelIndex());
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 2);
        // We can't make any assumptions about the order of the indexes
        // QCOMPARE(model->index(1, 0).data(Sink::Store::DomainObjectRole).value<Mail::Ptr>()->getProperty("uid").toByteArray(), QByteArray("testSecond"));
    }

    void testReactToNewResource()
    {
        Sink::Query query;
        query.setFlags(Query::LiveQuery);
        auto model = Sink::Store::loadModel<Folder>(query);
        QTRY_COMPARE(model->rowCount(QModelIndex()), 0);

        auto res = DummyResource::create("");
        VERIFYEXEC(Sink::Store::create(res));
        auto folder = Folder::create(res.identifier());
        VERIFYEXEC(Sink::Store::create(folder));
        QTRY_COMPARE(model->rowCount(QModelIndex()), 1);

        VERIFYEXEC(Sink::Store::remove(res));
    }

    void testAccountFilter()
    {
        using namespace Sink;
        using namespace Sink::ApplicationDomain;

        //Setup
        QString accountName("name");
        QString accountIcon("icon");
        auto account1 = ApplicationDomainType::createEntity<SinkAccount>();
        account1.setAccountType("maildir");
        account1.setName(accountName);
        account1.setIcon(accountIcon);
        VERIFYEXEC(Store::create(account1));

        auto account2 = ApplicationDomainType::createEntity<SinkAccount>();
        account2.setAccountType("maildir");
        account2.setName(accountName);
        account2.setIcon(accountIcon);
        VERIFYEXEC(Store::create(account2));

        auto resource1 = ApplicationDomainType::createEntity<SinkResource>();
        resource1.setResourceType("sink.dummy");
        resource1.setAccount(account1);
        Store::create(resource1).exec().waitForFinished();

        auto resource2 = ApplicationDomainType::createEntity<SinkResource>();
        resource2.setResourceType("sink.dummy");
        resource2.setAccount(account2);
        Store::create(resource2).exec().waitForFinished();

        {
            Folder folder1(resource1.identifier());
            VERIFYEXEC(Sink::Store::create<Folder>(folder1));
            Folder folder2(resource2.identifier());
            VERIFYEXEC(Sink::Store::create<Folder>(folder2));
        }

        // Test
        Sink::Query query;
        query.resourceFilter<SinkResource::Account>(account1);

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto folders = Sink::Store::read<Folder>(query);
        QCOMPARE(folders.size(), 1);
    }

    void testSubquery()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        folder1.setSpecialPurpose(QByteArrayList() << "purpose1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto folder2 = Folder::createEntity<Folder>("sink.dummy.instance1");
        folder2.setSpecialPurpose(QByteArrayList() << "purpose2");
        VERIFYEXEC(Sink::Store::create<Folder>(folder2));

        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setUid("mail1");
            mail.setFolder(folder1);
            VERIFYEXEC(Sink::Store::create(mail));
        }
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setUid("mail2");
            mail.setFolder(folder2);
            VERIFYEXEC(Sink::Store::create(mail));
        }

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        //Setup two folders with a mail each, ensure we only get the mail from the folder that matches the folder filter.
        Query query;
        query.filter<Mail::Folder>(Sink::Query().containsFilter<Folder::SpecialPurpose>("purpose1"));
        query.request<Mail::Uid>();

        auto mails = Sink::Store::read<Mail>(query);
        QCOMPARE(mails.size(), 1);
        QCOMPARE(mails.first().getUid().toLatin1(), QByteArray("mail1"));
    }

    void testLiveSubquery()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        folder1.setSpecialPurpose(QByteArrayList() << "purpose1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto folder2 = Folder::createEntity<Folder>("sink.dummy.instance1");
        folder2.setSpecialPurpose(QByteArrayList() << "purpose2");
        VERIFYEXEC(Sink::Store::create<Folder>(folder2));

        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setUid("mail1");
            mail.setFolder(folder1);
            VERIFYEXEC(Sink::Store::create(mail));
        }
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setUid("mail2");
            mail.setFolder(folder2);
            VERIFYEXEC(Sink::Store::create(mail));
        }

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        //Setup two folders with a mail each, ensure we only get the mail from the folder that matches the folder filter.
        Query query;
        query.filter<Mail::Folder>(Sink::Query().containsFilter<Folder::SpecialPurpose>("purpose1"));
        query.request<Mail::Uid>();
        query.setFlags(Query::LiveQuery);

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_COMPARE(model->rowCount(), 1);

        //This folder should not make it through the query
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setUid("mail3");
            mail.setFolder(folder2);
            VERIFYEXEC(Sink::Store::create(mail));
        }

        //But this one should
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setUid("mail4");
            mail.setFolder(folder1);
            VERIFYEXEC(Sink::Store::create(mail));
        }
        QTRY_COMPARE(model->rowCount(), 2);

    }

    void testResourceSubQuery()
    {
        using namespace Sink;
        using namespace Sink::ApplicationDomain;

        //Setup
        auto resource1 = ApplicationDomainType::createEntity<SinkResource>();
        resource1.setResourceType("sink.dummy");
        resource1.setCapabilities(QByteArrayList() << "cap1");
        VERIFYEXEC(Store::create(resource1));

        auto resource2 = ApplicationDomainType::createEntity<SinkResource>();
        resource2.setCapabilities(QByteArrayList() << "cap2");
        resource2.setResourceType("sink.dummy");
        VERIFYEXEC(Store::create(resource2));

        Folder folder1(resource1.identifier());
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));
        Folder folder2(resource2.identifier());
        VERIFYEXEC(Sink::Store::create<Folder>(folder2));

        // Test
        Sink::Query query;
        query.resourceContainsFilter<SinkResource::Capabilities>("cap1");

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto folders = Sink::Store::read<Folder>(query);
        QCOMPARE(folders.size(), 1);
    }
};

QTEST_MAIN(QueryTest)
#include "querytest.moc"
