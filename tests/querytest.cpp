#include <QtTest>

#include <QString>
#include <QSignalSpy>

#include "resource.h"
#include "store.h"
#include "resourcecontrol.h"
#include "commands.h"
#include "resourceconfig.h"
#include "log.h"
#include "modelresult.h"
#include "test.h"
#include "testutils.h"
#include "applicationdomaintype.h"

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
            mail.setExtractedMessageId("test1");
            mail.setFolder("folder1");
            Sink::Store::create<Mail>(mail).exec().waitForFinished();
        }
        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedMessageId("test2");
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
            auto folder = ApplicationDomainType::createEntity<Folder>("sink.dummy.instance1");
            VERIFYEXEC(Sink::Store::create<Folder>(folder));
            auto subfolder = ApplicationDomainType::createEntity<Folder>("sink.dummy.instance1");
            subfolder.setParent(folder.identifier());
            VERIFYEXEC(Sink::Store::create<Folder>(subfolder));
            // Ensure all local data is processed
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));
        }

        // Test
        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");
        query.requestTree<Folder::Parent>();

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

    void testMailByMessageId()
    {
        // Setup
        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedMessageId("test1");
            mail.setProperty("sender", "doe@example.org");
            Sink::Store::create<Mail>(mail).exec().waitForFinished();
        }

        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedMessageId("test2");
            mail.setProperty("sender", "doe@example.org");
            Sink::Store::create<Mail>(mail).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");
        query.filter<Mail::MessageId>("test1");

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
            mail.setExtractedMessageId("test1");
            mail.setFolder(folderEntity->identifier());
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
    void testMailByMessageIdAndFolder()
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
            mail.setExtractedMessageId("test1");
            mail.setFolder(folderEntity->identifier());
            Sink::Store::create<Mail>(mail).exec().waitForFinished();

            Mail mail1("sink.dummy.instance1");
            mail1.setExtractedMessageId("test1");
            mail1.setFolder("foobar");
            Sink::Store::create<Mail>(mail1).exec().waitForFinished();

            Mail mail2("sink.dummy.instance1");
            mail2.setExtractedMessageId("test2");
            mail2.setFolder(folderEntity->identifier());
            Sink::Store::create<Mail>(mail2).exec().waitForFinished();
        }

        // Test
        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");
        query.filter<Mail::Folder>(*folderEntity);
        query.filter<Mail::MessageId>("test1");

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
                mail.setExtractedMessageId("testSecond");
                mail.setFolder(folderEntity->identifier());
                mail.setExtractedDate(date.addDays(-1));
                Sink::Store::create<Mail>(mail).exec().waitForFinished();
            }
            {
                Mail mail("sink.dummy.instance1");
                mail.setExtractedMessageId("testLatest");
                mail.setFolder(folderEntity->identifier());
                mail.setExtractedDate(date);
                Sink::Store::create<Mail>(mail).exec().waitForFinished();
            }
            {
                Mail mail("sink.dummy.instance1");
                mail.setExtractedMessageId("testLast");
                mail.setFolder(folderEntity->identifier());
                mail.setExtractedDate(date.addDays(-2));
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
        QCOMPARE(model->index(0, 0).data(Sink::Store::DomainObjectRole).value<Mail::Ptr>()->getProperty("messageId").toByteArray(), QByteArray("testLatest"));

        model->fetchMore(QModelIndex());
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 2);
        // We can't make any assumptions about the order of the indexes
        // QCOMPARE(model->index(1, 0).data(Sink::Store::DomainObjectRole).value<Mail::Ptr>()->getProperty("messageId").toByteArray(), QByteArray("testSecond"));
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
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << resource1.identifier()));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << resource2.identifier()));

        // Test
        Sink::Query query;
        query.resourceFilter<SinkResource::Account>(account1);

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
            mail.setExtractedMessageId("mail1");
            mail.setFolder(folder1);
            VERIFYEXEC(Sink::Store::create(mail));
        }
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("mail2");
            mail.setFolder(folder2);
            VERIFYEXEC(Sink::Store::create(mail));
        }

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        //Setup two folders with a mail each, ensure we only get the mail from the folder that matches the folder filter.
        Query query;
        query.filter<Mail::Folder>(Sink::Query().containsFilter<Folder::SpecialPurpose>("purpose1"));
        query.request<Mail::MessageId>();

        auto mails = Sink::Store::read<Mail>(query);
        QCOMPARE(mails.size(), 1);
        QCOMPARE(mails.first().getMessageId(), QByteArray("mail1"));
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
            mail.setExtractedMessageId("mail1");
            mail.setFolder(folder1);
            VERIFYEXEC(Sink::Store::create(mail));
        }
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("mail2");
            mail.setFolder(folder2);
            VERIFYEXEC(Sink::Store::create(mail));
        }

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        //Setup two folders with a mail each, ensure we only get the mail from the folder that matches the folder filter.
        Query query;
        query.filter<Mail::Folder>(Sink::Query().containsFilter<Folder::SpecialPurpose>("purpose1"));
        query.request<Mail::MessageId>();
        query.setFlags(Query::LiveQuery);

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_COMPARE(model->rowCount(), 1);

        //This folder should not make it through the query
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("mail3");
            mail.setFolder(folder2);
            VERIFYEXEC(Sink::Store::create(mail));
        }

        //But this one should
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("mail4");
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

        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << resource1.identifier()));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << resource2.identifier()));

        // Test
        Sink::Query query;
        query.resourceContainsFilter<SinkResource::Capabilities>("cap1");

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto folders = Sink::Store::read<Folder>(query);
        QCOMPARE(folders.size(), 1);
    }

    void testLivequeryUnmatchInThread()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto folder2 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder2));

        auto mail1 = Mail::createEntity<Mail>("sink.dummy.instance1");
        mail1.setExtractedMessageId("mail1");
        mail1.setFolder(folder1);
        VERIFYEXEC(Sink::Store::create(mail1));
        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        //Setup two folders with a mail each, ensure we only get the mail from the folder that matches the folder filter.
        Query query;
        query.setId("testLivequeryUnmatch");
        query.filter<Mail::Folder>(folder1);
        query.reduce<Mail::ThreadId>(Query::Reduce::Selector::max<Mail::Date>()).count("count").collect<Mail::Sender>("senders");
        query.sort<Mail::Date>();
        query.setFlags(Query::LiveQuery);
        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_COMPARE(model->rowCount(), 1);

        //After the modifcation the mail should have vanished.
        {

            mail1.setFolder(folder2);
            VERIFYEXEC(Sink::Store::modify(mail1));
        }
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->rowCount(), 0);
    }

    void testDontUpdateNonLiveQuery()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto mail1 = Mail::createEntity<Mail>("sink.dummy.instance1");
        mail1.setExtractedMessageId("mail1");
        mail1.setFolder(folder1);
        mail1.setUnread(false);
        VERIFYEXEC(Sink::Store::create(mail1));

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        //Not a live query
        query.setFlags(Query::Flags{});
        query.setId("testNoLiveQuery");
        query.filter<Mail::Folder>(folder1);
        query.reduce<Mail::ThreadId>(Query::Reduce::Selector::max<Mail::Date>()).count("count").collect<Mail::Sender>("senders");
        query.sort<Mail::Date>();
        query.request<Mail::Unread>();
        QVERIFY(!query.liveQuery());

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_COMPARE(model->rowCount(), 1);

        //After the modifcation the mail should have vanished.
        {
            mail1.setUnread(true);
            VERIFYEXEC(Sink::Store::modify(mail1));
        }
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->rowCount(), 1);
        auto mail = model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>();
        QTest::qWait(100);
        QCOMPARE(mail->getUnread(), false);
    }

    void testLivequeryModifcationUpdateInThread()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto folder2 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder2));

        auto mail1 = Mail::createEntity<Mail>("sink.dummy.instance1");
        mail1.setExtractedMessageId("mail1");
        mail1.setFolder(folder1);
        mail1.setUnread(false);
        VERIFYEXEC(Sink::Store::create(mail1));

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setId("testLivequeryUnmatch");
        query.filter<Mail::Folder>(folder1);
        query.reduce<Mail::ThreadId>(Query::Reduce::Selector::max<Mail::Date>()).count("count").collect<Mail::Folder>("folders");
        query.sort<Mail::Date>();
        query.setFlags(Query::LiveQuery);
        query.request<Mail::Unread>();

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_COMPARE(model->rowCount(), 1);

        //After the modifcation the mail should have vanished.
        {
            mail1.setUnread(true);
            VERIFYEXEC(Sink::Store::modify(mail1));
        }
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->rowCount(), 1);
        auto mail = model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>();
        QTRY_COMPARE(mail->getUnread(), true);
        QCOMPARE(mail->getProperty("count").toInt(), 1);
        QCOMPARE(mail->getProperty("folders").toList().size(), 1);
    }

    void testReductionUpdate()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto folder2 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder2));

        QDateTime now{QDate{2017, 2, 3}, QTime{10, 0, 0}};
        QDateTime later{QDate{2017, 2, 3}, QTime{11, 0, 0}};

        auto mail1 = Mail::createEntity<Mail>("sink.dummy.instance1");
        mail1.setExtractedMessageId("mail1");
        mail1.setFolder(folder1);
        mail1.setUnread(false);
        mail1.setExtractedDate(now);
        VERIFYEXEC(Sink::Store::create(mail1));

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setId("testLivequeryUnmatch");
        query.setFlags(Query::LiveQuery);
        query.filter<Mail::Folder>(folder1);
        query.reduce<Mail::Folder>(Query::Reduce::Selector::max<Mail::Date>()).count("count").collect<Mail::Folder>("folders");
        query.sort<Mail::Date>();
        query.request<Mail::Unread>();
        query.request<Mail::MessageId>();

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);

        QSignalSpy insertedSpy(model.data(), &QAbstractItemModel::rowsInserted);
        QSignalSpy removedSpy(model.data(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy changedSpy(model.data(), &QAbstractItemModel::dataChanged);
        QSignalSpy layoutChangedSpy(model.data(), &QAbstractItemModel::layoutChanged);
        QSignalSpy resetSpy(model.data(), &QAbstractItemModel::modelReset);

        //The leader should change to mail2 after the modification
        {
            auto mail2 = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail2.setExtractedMessageId("mail2");
            mail2.setFolder(folder1);
            mail2.setUnread(false);
            mail2.setExtractedDate(later);
            VERIFYEXEC(Sink::Store::create(mail2));
        }

        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->rowCount(), 1);
        auto mail = model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>();
        QTRY_COMPARE(mail->getMessageId(), QByteArray{"mail2"});
        QCOMPARE(mail->getProperty("count").toInt(), 2);
        QCOMPARE(mail->getProperty("folders").toList().size(), 2);

        //This should eventually be just one modification instead of remove + add (See datastorequery reduce component)
        QCOMPARE(insertedSpy.size(), 1);
        QCOMPARE(removedSpy.size(), 1);
        QCOMPARE(changedSpy.size(), 0);
        QCOMPARE(layoutChangedSpy.size(), 0);
        QCOMPARE(resetSpy.size(), 0);
    }

    void testBloom()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto folder2 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder2));

        auto mail1 = Mail::createEntity<Mail>("sink.dummy.instance1");
        mail1.setExtractedMessageId("mail1");
        mail1.setFolder(folder1);
        VERIFYEXEC(Sink::Store::create(mail1));

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("mail2");
            mail.setFolder(folder1);
            VERIFYEXEC(Sink::Store::create(mail));
        }
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("mail3");
            mail.setFolder(folder2);
            VERIFYEXEC(Sink::Store::create(mail));
        }
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setId("testFilterCreationInThread");
        query.filter(mail1.identifier());
        query.bloom<Mail::Folder>();
        query.request<Mail::Folder>();

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 2);
    }

    void testLivequeryFilterCreationInThread()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto folder2 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder2));

        auto mail1 = Mail::createEntity<Mail>("sink.dummy.instance1");
        mail1.setExtractedMessageId("mail1");
        mail1.setFolder(folder1);
        VERIFYEXEC(Sink::Store::create(mail1));

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setId("testFilterCreationInThread");
        query.filter(mail1.identifier());
        query.bloom<Mail::Folder>();
        query.sort<Mail::Date>();
        query.setFlags(Query::LiveQuery);
        query.request<Mail::Unread>();
        query.request<Mail::Folder>();

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);

        QSignalSpy insertedSpy(model.data(), &QAbstractItemModel::rowsInserted);
        QSignalSpy removedSpy(model.data(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy changedSpy(model.data(), &QAbstractItemModel::dataChanged);
        QSignalSpy layoutChangedSpy(model.data(), &QAbstractItemModel::layoutChanged);
        QSignalSpy resetSpy(model.data(), &QAbstractItemModel::modelReset);

        //This modification should make it through
        {
            //This should not trigger an entity already in model warning
            mail1.setUnread(false);
            VERIFYEXEC(Sink::Store::modify(mail1));
        }

        //This mail should make it through
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("mail2");
            mail.setFolder(folder1);
            VERIFYEXEC(Sink::Store::create(mail));
        }

        //This mail shouldn't make it through
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("mail3");
            mail.setFolder(folder2);
            VERIFYEXEC(Sink::Store::create(mail));
        }
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        QTRY_COMPARE(model->rowCount(), 2);
        QTest::qWait(100);
        QCOMPARE(model->rowCount(), 2);

        //From mail2
        QCOMPARE(insertedSpy.size(), 1);
        QCOMPARE(removedSpy.size(), 0);
        //From the modification
        QCOMPARE(changedSpy.size(), 1);
        QCOMPARE(layoutChangedSpy.size(), 0);
        QCOMPARE(resetSpy.size(), 0);
    }

    void testLivequeryThreadleaderChange()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto folder2 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder2));

        QDateTime earlier{QDate{2017, 2, 3}, QTime{9, 0, 0}};
        QDateTime now{QDate{2017, 2, 3}, QTime{10, 0, 0}};
        QDateTime later{QDate{2017, 2, 3}, QTime{11, 0, 0}};

        auto mail1 = Mail::createEntity<Mail>("sink.dummy.instance1");
        mail1.setExtractedMessageId("mail1");
        mail1.setFolder(folder1);
        mail1.setExtractedDate(now);
        VERIFYEXEC(Sink::Store::create(mail1));

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setId("testLivequeryThreadleaderChange");
        query.setFlags(Query::LiveQuery);
        query.reduce<Mail::Folder>(Query::Reduce::Selector::max<Mail::Date>()).count("count").collect<Mail::Folder>("folders");
        query.sort<Mail::Date>();
        query.request<Mail::MessageId>();

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);

        QSignalSpy insertedSpy(model.data(), &QAbstractItemModel::rowsInserted);
        QSignalSpy removedSpy(model.data(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy changedSpy(model.data(), &QAbstractItemModel::dataChanged);
        QSignalSpy layoutChangedSpy(model.data(), &QAbstractItemModel::layoutChanged);
        QSignalSpy resetSpy(model.data(), &QAbstractItemModel::modelReset);

        //The leader shouldn't change to mail2 after the modification
        {
            auto mail2 = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail2.setExtractedMessageId("mail2");
            mail2.setFolder(folder1);
            mail2.setExtractedDate(earlier);
            VERIFYEXEC(Sink::Store::create(mail2));
        }

        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->rowCount(), 1);
        {
            auto mail = model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>();
            QTRY_COMPARE(mail->getMessageId(), QByteArray{"mail1"});
            QTRY_COMPARE(mail->getProperty("count").toInt(), 2);
            QCOMPARE(mail->getProperty("folders").toList().size(), 2);
        }


        QCOMPARE(insertedSpy.size(), 0);
        QCOMPARE(removedSpy.size(), 0);
        QCOMPARE(changedSpy.size(), 1);
        QCOMPARE(layoutChangedSpy.size(), 0);
        QCOMPARE(resetSpy.size(), 0);

        //The leader should change to mail3 after the modification
        {
            auto mail3 = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail3.setExtractedMessageId("mail3");
            mail3.setFolder(folder1);
            mail3.setExtractedDate(later);
            VERIFYEXEC(Sink::Store::create(mail3));
        }

        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->rowCount(), 1);
        {
            auto mail = model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>();
            QTRY_COMPARE(mail->getMessageId(), QByteArray{"mail3"});
            QCOMPARE(mail->getProperty("count").toInt(), 3);
            QCOMPARE(mail->getProperty("folders").toList().size(), 3);
        }

        //This should eventually be just one modification instead of remove + add (See datastorequery reduce component)
        QCOMPARE(insertedSpy.size(), 1);
        QCOMPARE(removedSpy.size(), 1);
        QCOMPARE(changedSpy.size(), 1);
        QCOMPARE(layoutChangedSpy.size(), 0);
        QCOMPARE(resetSpy.size(), 0);

        //Nothing should change on third mail in separate folder
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("mail4");
            mail.setFolder(folder2);
            mail.setExtractedDate(now);
            VERIFYEXEC(Sink::Store::create(mail));
        }

        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->rowCount(), 2);

        //This should eventually be just one modification instead of remove + add (See datastorequery reduce component)
        QCOMPARE(insertedSpy.size(), 2);
        QCOMPARE(removedSpy.size(), 1);
        QCOMPARE(changedSpy.size(), 1);
        QCOMPARE(layoutChangedSpy.size(), 0);
        QCOMPARE(resetSpy.size(), 0);
    }

    /*
     * Ensure that we handle the situation properly if the thread-leader doesn't match a property filter.
     */
    void testFilteredThreadLeader()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto folder2 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder2));

        QDateTime earlier{QDate{2017, 2, 3}, QTime{9, 0, 0}};
        QDateTime now{QDate{2017, 2, 3}, QTime{10, 0, 0}};
        QDateTime later{QDate{2017, 2, 3}, QTime{11, 0, 0}};

        {
            auto mail1 = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail1.setExtractedMessageId("mail1");
            mail1.setFolder(folder1);
            mail1.setExtractedDate(now);
            mail1.setImportant(false);
            VERIFYEXEC(Sink::Store::create(mail1));
        }
        {
            auto mail2 = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail2.setExtractedMessageId("mail2");
            mail2.setFolder(folder1);
            mail2.setExtractedDate(earlier);
            mail2.setImportant(false);
            VERIFYEXEC(Sink::Store::create(mail2));
        }
        {
            auto mail3 = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail3.setExtractedMessageId("mail3");
            mail3.setFolder(folder1);
            mail3.setExtractedDate(later);
            mail3.setImportant(true);
            VERIFYEXEC(Sink::Store::create(mail3));
        }

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setId("testLivequeryThreadleaderChange");
        query.setFlags(Query::LiveQuery);
        query.reduce<Mail::Folder>(Query::Reduce::Selector::max<Mail::Date>()).count("count").collect<Mail::Folder>("folders");
        query.sort<Mail::Date>();
        query.request<Mail::MessageId>();
        query.filter<Mail::Important>(false);

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());

        QCOMPARE(model->rowCount(), 1);

        {
            auto mail = model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>();
            QCOMPARE(mail->getMessageId(), QByteArray{"mail1"});
            QCOMPARE(mail->getProperty("count").toInt(), 2);
            QCOMPARE(mail->getProperty("folders").toList().size(), 2);
        }
    }
};

QTEST_MAIN(QueryTest)
#include "querytest.moc"
