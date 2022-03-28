#include <QTest>

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
#include "applicationdomaintype.h"
#include "queryrunner.h"
#include "adaptorfactoryregistry.h"
#include "fulltextindex.h"

#include <KMime/Message>
#include <KCalCore/Event>
#include <KCalCore/ICalFormat>

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
        qRegisterMetaType<QList<QPersistentModelIndex>>("QList<QPersistentModelIndex>");
        qRegisterMetaType<QAbstractItemModel::LayoutChangeHint>("QAbstractItemModel::LayoutChangeHint");
        Sink::Test::initTest();
        auto factory = Sink::ResourceFactory::load("sink.dummy");
        QVERIFY(factory);
        ResourceConfig::addResource("sink.dummy.instance1", "sink.dummy");
        ResourceConfig::configureResource("sink.dummy.instance1", {{"populate", true}});
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
        filter.propertyFilter.insert({"foo"}, QVariant::fromValue(QByteArray("bar")));

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
        auto mail = Mail("sink.dummy.instance1");
        mail.setExtractedMessageId("test1");
        VERIFYEXEC(Sink::Store::create<Mail>(mail));

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
        auto mail = Mail("sink.dummy.instance1");
        mail.setExtractedMessageId("test1");
        VERIFYEXEC(Sink::Store::create<Mail>(mail));

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
            VERIFYEXEC(Sink::Store::create<Mail>(mail));
        }
        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedMessageId("test2");
            mail.setFolder("folder2");
            VERIFYEXEC(Sink::Store::create<Mail>(mail));
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
            VERIFYEXEC(Sink::Store::modify<Mail>(*mail));
        }
        QTRY_COMPARE(model->rowCount(), 0);

        {
            mail->setFolder("folder1");
            VERIFYEXEC(Sink::Store::modify<Mail>(*mail));
        }
        QTRY_COMPARE(model->rowCount(), 1);
    }

    void testById()
    {
        QByteArray id;
        // Setup
        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedMessageId("test1");
            VERIFYEXEC(Sink::Store::create<Mail>(mail));
            mail.setExtractedMessageId("test2");
            VERIFYEXEC(Sink::Store::create<Mail>(mail));

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
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter(id);
            auto model = Sink::Store::loadModel<Mail>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);
        }

        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            //Try a non-existing id
            query.filter("{87fcea5e-8d2e-408e-bb8d-b27b9dcf5e92}");
            auto model = Sink::Store::loadModel<Mail>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 0);
        }
    }

    void testFolder()
    {
        // Setup
        {
            Folder folder("sink.dummy.instance1");
            VERIFYEXEC(Sink::Store::create<Folder>(folder));
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
        QCOMPARE(model->rowCount(model->index(0, 0)), 1);
    }

    void testIncrementalFolderTree()
    {
        // Setup
        auto folder = ApplicationDomainType::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder));
        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        // Test
        Sink::Query query{Sink::Query::LiveQuery};
        query.resourceFilter("sink.dummy.instance1");
        query.requestTree<Folder::Parent>();

        auto model = Sink::Store::loadModel<Folder>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);

        auto subfolder = ApplicationDomainType::createEntity<Folder>("sink.dummy.instance1");
        subfolder.setParent(folder.identifier());
        VERIFYEXEC(Sink::Store::create<Folder>(subfolder));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        //Ensure the folder appears
        QTRY_COMPARE(model->rowCount(model->index(0, 0)), 1);

        //...and dissapears again after removal
        VERIFYEXEC(Sink::Store::remove<Folder>(subfolder));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->rowCount(model->index(0, 0)), 0);
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
        const auto date = QDateTime(QDate(2015, 7, 7), QTime(12, 0));
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
        query.setFlags(Query::LiveQuery);
        query.reduce<ApplicationDomain::Mail::ThreadId>(Query::Reduce::Selector::max<ApplicationDomain::Mail::Date>())
            .count("count")
            .collect<ApplicationDomain::Mail::Unread>("unreadCollected")
            .collect<ApplicationDomain::Mail::Important>("importantCollected");

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

        //New revisions always go through
        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedMessageId("testInjected");
            mail.setFolder(folderEntity->identifier());
            mail.setExtractedDate(date.addDays(-2));
            Sink::Store::create<Mail>(mail).exec().waitForFinished();
        }
        QTRY_COMPARE(model->rowCount(), 3);

        //Ensure we can continue fetching after the incremental update
        model->fetchMore(QModelIndex());
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 4);

        //Ensure we have fetched all
        model->fetchMore(QModelIndex());
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 4);
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

        VERIFYEXEC(Sink::Store::create<Folder>(Folder{resource1.identifier()}));
        VERIFYEXEC(Sink::Store::create<Folder>(Folder{resource2.identifier()}));

        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(resource1.identifier()));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(resource2.identifier()));

        // We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto folders = Sink::Store::read<Folder>(Sink::Query{}.resourceContainsFilter<SinkResource::Capabilities>("cap1"));
        QCOMPARE(folders.size(), 1);

        //TODO this should be part of the regular cleanup between tests
        VERIFYEXEC(Store::remove(resource1));
        VERIFYEXEC(Store::remove(resource2));
    }

    void testFilteredLiveResourceSubQuery()
    {
        using namespace Sink;
        using namespace Sink::ApplicationDomain;

        //Setup
        auto resource1 = ApplicationDomainType::createEntity<SinkResource>();
        resource1.setResourceType("sink.dummy");
        resource1.setCapabilities(QByteArrayList() << "cap1");
        VERIFYEXEC(Store::create(resource1));
        VERIFYEXEC(Store::create<Folder>(Folder{resource1.identifier()}));
        VERIFYEXEC(ResourceControl::flushMessageQueue(resource1.identifier()));

        auto model = Sink::Store::loadModel<Folder>(Query{Query::LiveQuery}.resourceContainsFilter<SinkResource::Capabilities>("cap1"));
        QTRY_COMPARE(model->rowCount(), 1);

        auto resource2 = ApplicationDomainType::createEntity<SinkResource>();
        resource2.setCapabilities(QByteArrayList() << "cap2");
        resource2.setResourceType("sink.dummy");
        VERIFYEXEC(Store::create(resource2));
        VERIFYEXEC(Store::create<Folder>(Folder{resource2.identifier()}));
        VERIFYEXEC(ResourceControl::flushMessageQueue(resource2.identifier()));

        //The new resource should be filtered and thus not make it in here
        QCOMPARE(model->rowCount(), 1);

        //TODO this should be part of the regular cleanup between tests
        VERIFYEXEC(Store::remove(resource1));
        VERIFYEXEC(Store::remove(resource2));
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

    void testLivequeryFilterUnrelated()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto mail1 = Mail::createEntity<Mail>("sink.dummy.instance1");
        mail1.setExtractedMessageId("mail1");
        mail1.setFolder(folder1);
        VERIFYEXEC(Sink::Store::create(mail1));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setId("testLivequeryUnmatch");
        query.filter(mail1.identifier());
        query.setFlags(Query::LiveQuery);
        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_COMPARE(model->rowCount(), 1);

        //Create another mail and make sure it doesn't show up in the query
        auto mail2 = Mail::createEntity<Mail>("sink.dummy.instance1");
        mail2.setExtractedMessageId("mail2");
        mail2.setFolder(folder1);
        VERIFYEXEC(Sink::Store::create(mail2));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        QCOMPARE(model->rowCount(), 1);

        //A removal should still make it though
        {
            VERIFYEXEC(Sink::Store::remove(mail1));
        }
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->rowCount(), 0);
    }


    void testLivequeryRemoveOneInThread()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto mail1 = Mail::createEntity<Mail>("sink.dummy.instance1");
        mail1.setExtractedMessageId("mail1");
        mail1.setFolder(folder1);
        VERIFYEXEC(Sink::Store::create(mail1));
        auto mail2 = Mail::createEntity<Mail>("sink.dummy.instance1");
        mail2.setExtractedMessageId("mail2");
        mail2.setFolder(folder1);
        VERIFYEXEC(Sink::Store::create(mail2));
        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        //Setup two folders with a mail each, ensure we only get the mail from the folder that matches the folder filter.
        Query query;
        query.setId("testLivequeryUnmatch");
        query.reduce<Mail::Folder>(Query::Reduce::Selector::max<Mail::Date>()).count("count").collect<Mail::Sender>("senders");
        query.sort<Mail::Date>();
        query.setFlags(Query::LiveQuery);
        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_COMPARE(model->rowCount(), 1);
        QCOMPARE(model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>()->getProperty("count").toInt(), 2);

        //After the removal, the thread size should be reduced by one
        {

            VERIFYEXEC(Sink::Store::remove(mail1));
        }
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->rowCount(), 1);
        QTRY_COMPARE(model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>()->getProperty("count").toInt(), 1);

        //After the second removal, the thread should be gone
        {
            VERIFYEXEC(Sink::Store::remove(mail2));
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

    void testFilteredReductionUpdate()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto folder2 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder2));

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setId("testFilteredReductionUpdate");
        query.setFlags(Query::LiveQuery);
        query.filter<Mail::Folder>(folder1);
        query.reduce<Mail::Folder>(Query::Reduce::Selector::max<Mail::Date>()).count("count").collect<Mail::Folder>("folders");
        query.sort<Mail::Date>();

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 0);

        QSignalSpy insertedSpy(model.data(), &QAbstractItemModel::rowsInserted);
        QSignalSpy removedSpy(model.data(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy changedSpy(model.data(), &QAbstractItemModel::dataChanged);
        QSignalSpy layoutChangedSpy(model.data(), &QAbstractItemModel::layoutChanged);
        QSignalSpy resetSpy(model.data(), &QAbstractItemModel::modelReset);

        //Ensure we don't end up with a mail in the thread that was filtered
        //This tests the case of an otherwise emtpy thread on purpose.
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("filtered");
            mail.setFolder(folder2);
            mail.setExtractedDate(QDateTime{QDate{2017, 2, 3}, QTime{11, 0, 0}});
            VERIFYEXEC(Sink::Store::create(mail));
        }

        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QCOMPARE(model->rowCount(), 0);

        //Ensure the non-filtered still get through.
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("not-filtered");
            mail.setFolder(folder1);
            mail.setExtractedDate(QDateTime{QDate{2017, 2, 3}, QTime{11, 0, 0}});
            VERIFYEXEC(Sink::Store::create(mail));
        }
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->rowCount(), 1);
    }

    /*
     * Two messages in the same thread. The first get's filtered, the second one makes it.
     */
    void testFilteredReductionUpdateInSameThread()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        auto folder2 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder2));

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setId("testFilteredReductionUpdate");
        query.setFlags(Query::LiveQuery);
        query.filter<Mail::Folder>(folder1);
        query.reduce<Mail::MessageId>(Query::Reduce::Selector::max<Mail::Date>()).count("count");

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 0);

        //The first message will be filtered (but would be aggreagted together with the message that passes)
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("aggregatedId");
            mail.setFolder(folder2);
            VERIFYEXEC(Sink::Store::create(mail));

            //Ensure that we can deal with a modification to the filtered message
            mail.setUnread(true);
            VERIFYEXEC(Sink::Store::modify(mail));
        }

        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QCOMPARE(model->rowCount(), 0);

        //Ensure the non-filtered still gets through.
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("aggregatedId");
            mail.setFolder(folder1);
            VERIFYEXEC(Sink::Store::create(mail));

            //Ensure that we can deal with a modification to the filtered message
            mail.setUnread(true);
            VERIFYEXEC(Sink::Store::modify(mail));
        }
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->rowCount(), 1);
        QCOMPARE(model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>()->getProperty("count").toInt(), 1);

        //Ensure another entity still results in a modification
        {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId("aggregatedId");
            mail.setFolder(folder1);
            VERIFYEXEC(Sink::Store::create(mail));
        }
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        QTRY_COMPARE(model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>()->getProperty("count").toInt(), 2);
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
        query.resourceFilter("sink.dummy.instance1");
        query.setId("testFilterCreationInThread");
        query.filter(mail1.identifier());
        query.bloom<Mail::Folder>();
        query.request<Mail::Folder>();

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 2);
    }

    //Live query bloom filter
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
        mail1.setUnread(true);
        VERIFYEXEC(Sink::Store::create(mail1));

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setId("testFilterCreationInThread");
        query.resourceFilter("sink.dummy.instance1");
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

    //Live query reduction
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

        auto createMail = [] (const QByteArray &messageid, const Folder &folder, const QDateTime &date, bool important) {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedSubject(messageid);
            mail.setExtractedMessageId(messageid);
            mail.setFolder(folder);
            mail.setExtractedDate(date);
            mail.setImportant(important);
            return mail;
        };

        VERIFYEXEC(Sink::Store::create(createMail("mail1", folder1, now, false)));
        VERIFYEXEC(Sink::Store::create(createMail("mail2", folder1, earlier, false)));
        VERIFYEXEC(Sink::Store::create(createMail("mail3", folder1, later, true)));

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setId("testLivequeryThreadleaderChange");
        query.setFlags(Query::LiveQuery);
        query.reduce<Mail::Folder>(Query::Reduce::Selector::max<Mail::Date>())
            .count()
            .collect<Mail::Folder>()
            .select<Mail::Subject>(Query::Reduce::Selector::Min, "subjectSelected");
        query.sort<Mail::Date>();
        query.request<Mail::MessageId>();
        query.request<Mail::Subject>();
        query.filter<Mail::Important>(false);

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());

        QCOMPARE(model->rowCount(), 1);

        {
            auto mail = model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>();
            QCOMPARE(mail->getMessageId(), QByteArray{"mail1"});
            QCOMPARE(mail->count(), 2);
            QCOMPARE(mail->getCollectedProperty<Mail::Folder>().size(), 2);
            QCOMPARE(mail->getProperty("subjectSelected").toString(), QString{"mail2"});
        }
    }

    void testQueryRunnerDontMissUpdates()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));

        QDateTime now{QDate{2017, 2, 3}, QTime{10, 0, 0}};

        auto createMail = [] (const QByteArray &messageid, const Folder &folder, const QDateTime &date, bool important) {
            auto mail = Mail::createEntity<Mail>("sink.dummy.instance1");
            mail.setExtractedMessageId(messageid);
            mail.setFolder(folder);
            mail.setExtractedDate(date);
            mail.setImportant(important);
            return mail;
        };

        VERIFYEXEC(Sink::Store::create(createMail("mail1", folder1, now, false)));

        // Ensure all local data is processed
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setFlags(Query::LiveQuery);

        Sink::ResourceContext resourceContext{"sink.dummy.instance1", "sink.dummy", Sink::AdaptorFactoryRegistry::instance().getFactories("sink.dummy")};
        Sink::Log::Context logCtx;
        auto runner = new QueryRunner<Mail>(query, resourceContext, ApplicationDomain::getTypeName<Mail>(), logCtx);
        runner->delayNextQuery();

        auto emitter = runner->emitter();
        QList<Mail::Ptr> added;
        emitter->onAdded([&](Mail::Ptr mail) {
            added << mail;
        });

        emitter->fetch();
        VERIFYEXEC(Sink::Store::create(createMail("mail2", folder1, now, false)));
        QTRY_COMPARE(added.size(), 2);

        runner->delayNextQuery();
        VERIFYEXEC(Sink::Store::create(createMail("mail3", folder1, now, false)));
        //The second revision update is supposed to come in while the initial revision update is still in the query.
        //So wait a bit to make sure the query is currently runnning.
        QTest::qWait(500);
        VERIFYEXEC(Sink::Store::create(createMail("mail4", folder1, now, false)));
        QTRY_COMPARE(added.size(), 4);
    }

    /*
     * This test excercises the scenario where a fetchMore is triggered after
     * the revision is already updated in storage, but the incremental query was not run yet.
     * This resulted in lost modification updates.
     * It also exercised the lower bound protection, because we delay the update, and thus the resource will already have cleaned up.
     */
    void testQueryRunnerDontMissUpdatesWithFetchMore()
    {
        // Setup
        auto folder1 = Folder::createEntity<Folder>("sink.dummy.instance1");
        folder1.setName("name1");
        VERIFYEXEC(Sink::Store::create<Folder>(folder1));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Query query;
        query.setFlags(Query::LiveQuery);

        Sink::ResourceContext resourceContext{"sink.dummy.instance1", "sink.dummy", Sink::AdaptorFactoryRegistry::instance().getFactories("sink.dummy")};
        Sink::Log::Context logCtx;
        auto runner = new QueryRunner<Folder>(query, resourceContext, ApplicationDomain::getTypeName<Folder>(), logCtx);

        auto emitter = runner->emitter();
        QList<Folder::Ptr> added;
        emitter->onAdded([&](Folder::Ptr folder) {
            added << folder;
        });
        QList<Folder::Ptr> modified;
        emitter->onModified([&](Folder::Ptr folder) {
            modified << folder;
        });
        QList<Folder::Ptr> removed;
        emitter->onRemoved([&](Folder::Ptr folder) {
            removed << folder;
        });

        emitter->fetch();
        QTRY_COMPARE(added.size(), 1);
        QCOMPARE(modified.size(), 0);
        QCOMPARE(removed.size(), 0);

        runner->ignoreRevisionChanges();

        folder1.setName("name2");
        VERIFYEXEC(Sink::Store::modify<Folder>(folder1));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        emitter->fetch();

        runner->triggerRevisionChange();

        QTRY_COMPARE(added.size(), 1);
        QTRY_COMPARE(modified.size(), 1);
        QCOMPARE(removed.size(), 0);

        runner->ignoreRevisionChanges();
        VERIFYEXEC(Sink::Store::remove<Folder>(folder1));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        runner->triggerRevisionChange();
        QTRY_COMPARE(removed.size(), 1);
    }

    /*
     * This test is here to ensure we don't crash if we call removeFromDisk with a running query.
     */
    void testRemoveFromDiskWithRunningQuery()
    {
        // FIXME: we currently crash
        QSKIP("Skipping because this produces a crash.");
        {
            // Setup
            Folder::Ptr folderEntity;
            const auto date = QDateTime(QDate(2015, 7, 7), QTime(12, 0));
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

                //Add enough data so the query takes long enough that we remove the data from disk whlie the query is ongoing.
                for (int i = 0; i < 100; i++) {
                    Mail mail("sink.dummy.instance1");
                    mail.setExtractedMessageId("test" + QByteArray::number(i));
                    mail.setFolder(folderEntity->identifier());
                    mail.setExtractedDate(date.addDays(i));
                    Sink::Store::create<Mail>(mail).exec().waitForFinished();
                }
            }

            // Test
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Folder>(*folderEntity);
            query.sort<Mail::Date>();
            query.setFlags(Query::LiveQuery);
            query.reduce<ApplicationDomain::Mail::ThreadId>(Query::Reduce::Selector::max<ApplicationDomain::Mail::Date>())
                .count("count")
                .collect<ApplicationDomain::Mail::Unread>("unreadCollected")
                .collect<ApplicationDomain::Mail::Important>("importantCollected");

            // Ensure all local data is processed
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

            auto model = Sink::Store::loadModel<Mail>(query);
        }

        //FIXME: this will result in a crash in the above still running query.
        VERIFYEXEC(Sink::Store::removeDataFromDisk(QByteArray("sink.dummy.instance1")));
    }

    void testMailFulltext()
    {
        QByteArray id1;
        QByteArray id2;
        // Setup
        {
            {
                auto msg = KMime::Message::Ptr::create();
                msg->subject()->from7BitString("Subject To Search");
                msg->setBody("This is the searchable body bar. unique sender2");
                msg->from()->from7BitString("\"The Sender\"<sender@example.org>");
                msg->to()->from7BitString("\"Foo Bar\"<foo-bar@example.org>");
                msg->assemble();

                auto mail = ApplicationDomainType::createEntity<Mail>("sink.dummy.instance1");
                mail.setExtractedMessageId("test1");
                mail.setFolder("folder1");
                mail.setMimeMessage(msg->encodedContent());
                VERIFYEXEC(Sink::Store::create<Mail>(mail));
                id1 = mail.identifier();
            }
            {
                auto msg = KMime::Message::Ptr::create();
                msg->subject()->from7BitString("Stuff to Search");
                msg->setBody("Body foo bar");
                msg->from()->from7BitString("\"Another Sender2\"<sender2@unique.com>");
                msg->assemble();
                auto mail = ApplicationDomainType::createEntity<Mail>("sink.dummy.instance1");
                mail.setExtractedMessageId("test2");
                mail.setFolder("folder2");
                mail.setMimeMessage(msg->encodedContent());
                VERIFYEXEC(Sink::Store::create<Mail>(mail));
                id2 = mail.identifier();
            }
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
            {
                FulltextIndex index("sink.dummy.instance1", Sink::Storage::DataStore::ReadOnly);
                qInfo() << QString("Found document 1 with terms: ") + index.getIndexContent(id1).terms.join(", ");
                qInfo() << QString("Found document 2 with terms: ") + index.getIndexContent(id2).terms.join(", ");
            }
        }

        // Test
        // Default search
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Subject>(QueryBase::Comparator(QString("Subject To Search"), QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
        // Phrase search
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Subject>(QueryBase::Comparator(QString("\"Subject To Search\""), QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Subject>(QueryBase::Comparator(QString("\"Stuff to Search\""), QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
        }
        //Operators
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Subject>(QueryBase::Comparator(QString("subject AND search"), QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Subject>(QueryBase::Comparator(QString("subject OR search"), QueryBase::Comparator::Fulltext));
            QCOMPARE(Sink::Store::read<Mail>(query).size(), 2);
        }
        //Case-insensitive
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Subject>(QueryBase::Comparator(QString("Subject"), QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
        //Case-insensitive
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Subject>(QueryBase::Comparator(QString("subject"), QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
        //Partial match
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Subject>(QueryBase::Comparator(QString("subj"), QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
        //Filter by body
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::MimeMessage>(QueryBase::Comparator(QString("searchable"), QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
        //Filter by folder
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Subject>(QueryBase::Comparator(QString("Subject"), QueryBase::Comparator::Fulltext));
            query.filter<Mail::Folder>("folder1");
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
        //Filter by folder
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Subject>(QueryBase::Comparator(QString("Subject"), QueryBase::Comparator::Fulltext));
            query.filter<Mail::Folder>("folder2");
            QCOMPARE(Sink::Store::read<Mail>(query).size(), 0);
        }
        //Filter by sender
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter({}, Sink::QueryBase::Comparator(QString("sender"), Sink::QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 2);
        }
        //Filter by sender
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter({}, Sink::QueryBase::Comparator(QString("Sender"), Sink::QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 2);
        }
        //Filter by sender
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter({}, Sink::QueryBase::Comparator(QString("sender@example"), Sink::QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
        //Filter by sender
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter({}, Sink::QueryBase::Comparator(QString("The Sender"), Sink::QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
        }

        //Filter by sender
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter({}, Sink::QueryBase::Comparator(QString("sender2@unique.com"), Sink::QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id2);
        }

        //Filter by recipient
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter({}, Sink::QueryBase::Comparator(QString("foo-bar@example.org"), Sink::QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }

        //Filter by recipient
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter({}, Sink::QueryBase::Comparator(QString("foo-bar@example.com"), Sink::QueryBase::Comparator::Fulltext));
            QCOMPARE(Sink::Store::read<Mail>(query).size(), 0);
        }

        //Filter by subject field
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter({}, QueryBase::Comparator(QString("subject:\"Subject To Search\""), QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
        //Ensure the query searches the right field
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter({}, QueryBase::Comparator(QString("sender:\"Subject To Search\""), QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 0);
        }
    }

    void testUTF8MailFulltext()
    {
        QByteArray id1;
        // Setup
        {
            {
                auto msg = KMime::Message::Ptr::create();
                msg->subject()->fromUnicodeString("sübject", "utf8");
                msg->setBody("büdi");
                msg->from()->fromUnicodeString("\"John Düderli\"<john@doe.com>", "utf8");
                msg->assemble();

                auto mail = ApplicationDomainType::createEntity<Mail>("sink.dummy.instance1");
                mail.setExtractedMessageId("test1");
                mail.setFolder("folder1");
                mail.setMimeMessage(msg->encodedContent());
                VERIFYEXEC(Sink::Store::create<Mail>(mail));
                id1 = mail.identifier();
            }
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
            {
                FulltextIndex index("sink.dummy.instance1", Sink::Storage::DataStore::ReadOnly);
                qInfo() << QString("found document 1 with terms: ") + index.getIndexContent(id1).terms.join(", ");
            }
        }
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter({}, Sink::QueryBase::Comparator(QString("sübject"), Sink::QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter({}, Sink::QueryBase::Comparator(QString("büdi"), Sink::QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter({}, Sink::QueryBase::Comparator(QString("düderli"), Sink::QueryBase::Comparator::Fulltext));
            const auto list = Sink::Store::read<Mail>(query);
            QCOMPARE(list.size(), 1);
            QCOMPARE(list.first().identifier(), id1);
        }
    }

    void testLiveMailFulltext()
    {
        Sink::Query query;
        query.setFlags(Query::LiveQuery);
        query.resourceFilter("sink.dummy.instance1");
        query.filter<Mail::Subject>(QueryBase::Comparator(QString("Live Subject To Search"), QueryBase::Comparator::Fulltext));

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 0);
        Mail mailToModify;
        {
            {
                auto msg = KMime::Message::Ptr::create();
                msg->subject()->from7BitString("Not a match");
                msg->setBody("This is the searchable body bar. unique sender1");
                msg->from()->from7BitString("\"The Sender\"<sender@example.org>");
                msg->to()->from7BitString("\"Foo Bar\"<foo-bar@example.org>");
                msg->assemble();

                auto mail = ApplicationDomainType::createEntity<Mail>("sink.dummy.instance1");
                mail.setExtractedMessageId("test1");
                mail.setFolder("folder1");
                mail.setMimeMessage(msg->encodedContent());
                VERIFYEXEC(Sink::Store::create<Mail>(mail));
            }
            {
                auto msg = KMime::Message::Ptr::create();
                msg->subject()->from7BitString("Live Subject To Search");
                msg->setBody("This is the searchable body bar. unique sender2");
                msg->from()->from7BitString("\"The Sender\"<sender@example.org>");
                msg->to()->from7BitString("\"Foo Bar\"<foo-bar@example.org>");
                msg->assemble();

                auto mail = ApplicationDomainType::createEntity<Mail>("sink.dummy.instance1");
                mail.setExtractedMessageId("test1");
                mail.setFolder("folder1");
                mail.setMimeMessage(msg->encodedContent());
                mail.setUnread(true);
                VERIFYEXEC(Sink::Store::create<Mail>(mail));
                mailToModify = mail;
            }

            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        }
        QTRY_COMPARE(model->rowCount(), 1);
        //Test a modification that shouldn't affect the result
        {
            QSignalSpy insertedSpy(model.data(), &QAbstractItemModel::rowsInserted);
            QSignalSpy removedSpy(model.data(), &QAbstractItemModel::rowsRemoved);
            QSignalSpy changedSpy(model.data(), &QAbstractItemModel::dataChanged);
            QSignalSpy layoutChangedSpy(model.data(), &QAbstractItemModel::layoutChanged);
            QSignalSpy resetSpy(model.data(), &QAbstractItemModel::modelReset);

            mailToModify.setUnread(false);
            VERIFYEXEC(Sink::Store::modify(mailToModify));

            QTRY_COMPARE(changedSpy.size(), 1);
            QCOMPARE(insertedSpy.size(), 0);
            QCOMPARE(removedSpy.size(), 0);
            QCOMPARE(layoutChangedSpy.size(), 0);
            QCOMPARE(resetSpy.size(), 0);
        }
        //Test a modification that should affect the result
        {
            QSignalSpy insertedSpy(model.data(), &QAbstractItemModel::rowsInserted);
            QSignalSpy removedSpy(model.data(), &QAbstractItemModel::rowsRemoved);
            QSignalSpy changedSpy(model.data(), &QAbstractItemModel::dataChanged);
            QSignalSpy layoutChangedSpy(model.data(), &QAbstractItemModel::layoutChanged);
            QSignalSpy resetSpy(model.data(), &QAbstractItemModel::modelReset);

            auto msg = KMime::Message::Ptr::create();
            msg->subject()->from7BitString("No longer a match");
            msg->setBody("This is the searchable body bar. unique sender2");
            msg->from()->from7BitString("\"The Sender\"<sender@example.org>");
            msg->to()->from7BitString("\"Foo Bar\"<foo-bar@example.org>");
            msg->assemble();

            mailToModify.setMimeMessage(msg->encodedContent());
            VERIFYEXEC(Sink::Store::modify(mailToModify));

            QTRY_COMPARE(removedSpy.size(), 1);
            QCOMPARE(changedSpy.size(), 0);
            QCOMPARE(insertedSpy.size(), 0);
            QCOMPARE(layoutChangedSpy.size(), 0);
            QCOMPARE(resetSpy.size(), 0);
        }
        QCOMPARE(model->rowCount(), 0);
    }

    void testLiveMailFulltextThreaded()
    {
        Sink::Query query;
        query.setFlags(Query::LiveQuery);
        query.resourceFilter("sink.dummy.instance1");
        //Rely on partial matching
        query.filter<Mail::Subject>(QueryBase::Comparator(QString("LiveSubject"), QueryBase::Comparator::Fulltext));
        query.reduce<Mail::Folder>(Query::Reduce::Selector::max<Mail::Date>()).count("count").collect<Mail::Sender>("senders");

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 0);
        Mail mail1;
        Mail mail2;
        Mail mail3;
        {
            {
                auto msg = KMime::Message::Ptr::create();
                msg->subject()->from7BitString("Not a match");
                msg->setBody("This is the searchable body bar. unique sender1");
                msg->from()->from7BitString("\"The Sender\"<sender@example.org>");
                msg->to()->from7BitString("\"Foo Bar\"<foo-bar@example.org>");
                msg->assemble();

                auto mail = ApplicationDomainType::createEntity<Mail>("sink.dummy.instance1");
                mail.setExtractedMessageId("test1");
                mail.setFolder("folder1");
                mail.setMimeMessage(msg->encodedContent());
                mail.setUnread(true);
                VERIFYEXEC(Sink::Store::create<Mail>(mail));
                mail1 = mail;
            }
            {
                auto msg = KMime::Message::Ptr::create();
                msg->subject()->from7BitString("LiveSubjectToSearch");
                msg->setBody("This is the searchable body bar. unique sender2");
                msg->from()->from7BitString("\"The Sender\"<sender@example.org>");
                msg->to()->from7BitString("\"Foo Bar\"<foo-bar@example.org>");
                msg->assemble();

                auto mail = ApplicationDomainType::createEntity<Mail>("sink.dummy.instance1");
                mail.setExtractedMessageId("test2");
                mail.setFolder("folder1");
                mail.setMimeMessage(msg->encodedContent());
                mail.setUnread(true);
                VERIFYEXEC(Sink::Store::create<Mail>(mail));
                mail2 = mail;
            }
            {
                auto msg = KMime::Message::Ptr::create();
                msg->subject()->from7BitString("LiveSubjectToSearch");
                msg->setBody("This is the searchable body bar. unique sender2");
                msg->from()->from7BitString("\"The Sender\"<sender@example.org>");
                msg->to()->from7BitString("\"Foo Bar\"<foo-bar@example.org>");
                msg->assemble();

                auto mail = ApplicationDomainType::createEntity<Mail>("sink.dummy.instance1");
                mail.setExtractedMessageId("test3");
                mail.setFolder("folder2");
                mail.setMimeMessage(msg->encodedContent());
                mail.setUnread(true);
                VERIFYEXEC(Sink::Store::create<Mail>(mail));
                mail3 = mail;
            }

            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
        }
        QTRY_COMPARE(model->rowCount(), 2);
        //Test a modification that shouldn't affect the result
        {
            QSignalSpy insertedSpy(model.data(), &QAbstractItemModel::rowsInserted);
            QSignalSpy removedSpy(model.data(), &QAbstractItemModel::rowsRemoved);
            QSignalSpy changedSpy(model.data(), &QAbstractItemModel::dataChanged);
            QSignalSpy layoutChangedSpy(model.data(), &QAbstractItemModel::layoutChanged);
            QSignalSpy resetSpy(model.data(), &QAbstractItemModel::modelReset);

            mail2.setUnread(false);
            VERIFYEXEC(Sink::Store::modify(mail2));

            QTRY_COMPARE(changedSpy.size(), 1);
            QCOMPARE(insertedSpy.size(), 0);
            QCOMPARE(removedSpy.size(), 0);
            QCOMPARE(layoutChangedSpy.size(), 0);
            QCOMPARE(resetSpy.size(), 0);
        }

        //Test a modification that shouldn't affect the result
        {
            QSignalSpy insertedSpy(model.data(), &QAbstractItemModel::rowsInserted);
            QSignalSpy removedSpy(model.data(), &QAbstractItemModel::rowsRemoved);
            QSignalSpy changedSpy(model.data(), &QAbstractItemModel::dataChanged);
            QSignalSpy layoutChangedSpy(model.data(), &QAbstractItemModel::layoutChanged);
            QSignalSpy resetSpy(model.data(), &QAbstractItemModel::modelReset);

            mail1.setUnread(false);
            VERIFYEXEC(Sink::Store::modify(mail1));

            QTRY_COMPARE(changedSpy.size(), 1);
            QCOMPARE(insertedSpy.size(), 0);
            QCOMPARE(removedSpy.size(), 0);
            QCOMPARE(layoutChangedSpy.size(), 0);
            QCOMPARE(resetSpy.size(), 0);
        }

        {
            QSignalSpy insertedSpy(model.data(), &QAbstractItemModel::rowsInserted);
            QSignalSpy removedSpy(model.data(), &QAbstractItemModel::rowsRemoved);
            QSignalSpy changedSpy(model.data(), &QAbstractItemModel::dataChanged);
            QSignalSpy layoutChangedSpy(model.data(), &QAbstractItemModel::layoutChanged);
            QSignalSpy resetSpy(model.data(), &QAbstractItemModel::modelReset);

            mail3.setUnread(false);
            VERIFYEXEC(Sink::Store::modify(mail3));

            QTRY_COMPARE(changedSpy.size(), 1);
            QCOMPARE(insertedSpy.size(), 0);
            QCOMPARE(removedSpy.size(), 0);
            QCOMPARE(layoutChangedSpy.size(), 0);
            QCOMPARE(resetSpy.size(), 0);
        }

        //Test a modification that should affect the result
        {
            QSignalSpy insertedSpy(model.data(), &QAbstractItemModel::rowsInserted);
            QSignalSpy removedSpy(model.data(), &QAbstractItemModel::rowsRemoved);
            QSignalSpy changedSpy(model.data(), &QAbstractItemModel::dataChanged);
            QSignalSpy layoutChangedSpy(model.data(), &QAbstractItemModel::layoutChanged);
            QSignalSpy resetSpy(model.data(), &QAbstractItemModel::modelReset);

            auto msg = KMime::Message::Ptr::create();
            msg->subject()->from7BitString("No longer a match");
            msg->setBody("This is the searchable body bar. unique sender2");
            msg->from()->from7BitString("\"The Sender\"<sender@example.org>");
            msg->to()->from7BitString("\"Foo Bar\"<foo-bar@example.org>");
            msg->assemble();

            mail2.setMimeMessage(msg->encodedContent());
            VERIFYEXEC(Sink::Store::modify(mail2));

            QTRY_COMPARE(removedSpy.size(), 1);
            QCOMPARE(changedSpy.size(), 0);
            QCOMPARE(insertedSpy.size(), 0);
            QCOMPARE(layoutChangedSpy.size(), 0);
            QCOMPARE(resetSpy.size(), 0);
        }
        QCOMPARE(model->rowCount(), 1);
    }

    void mailsWithDates()
    {
        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedDate(QDateTime::fromString("2018-05-23T13:49:41Z", Qt::ISODate));
            mail.setExtractedMessageId("message1");
            VERIFYEXEC(Sink::Store::create<Mail>(mail));
        }
        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedDate(QDateTime::fromString("2018-05-23T13:50:00Z", Qt::ISODate));
            mail.setExtractedMessageId("message2");
            VERIFYEXEC(Sink::Store::create<Mail>(mail));
        }
        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedDate(QDateTime::fromString("2018-05-27T13:50:00Z", Qt::ISODate));
            mail.setExtractedMessageId("message3");
            VERIFYEXEC(Sink::Store::create<Mail>(mail));
        }
        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedMessageId("message4");
            VERIFYEXEC(Sink::Store::create<Mail>(mail));
        }
        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedDate(QDateTime::fromString("2078-05-23T13:49:41Z", Qt::ISODate));
            mail.setExtractedMessageId("message5");
            VERIFYEXEC(Sink::Store::create<Mail>(mail));
        }
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));
    }

    void testMailDate()
    {
        mailsWithDates();

        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Date>(QDateTime::fromString("2018-05-23T13:49:41Z", Qt::ISODate));
            auto model = Sink::Store::loadModel<Mail>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);
        }

        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Date>(QDateTime::fromString("2018-05-27T13:49:41Z", Qt::ISODate));
            auto model = Sink::Store::loadModel<Mail>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 0);
        }

        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Date>(QDateTime::fromString("2018-05-27T13:50:00Z", Qt::ISODate));
            auto model = Sink::Store::loadModel<Mail>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);
        }

    }

    void testMailRange()
    {
        mailsWithDates();

        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Date>(QueryBase::Comparator(QVariantList{QDateTime::fromString("2018-05-23T13:49:41Z", Qt::ISODate), QDateTime::fromString("2018-05-23T13:49:41Z", Qt::ISODate)}, QueryBase::Comparator::Within));
            auto model = Sink::Store::loadModel<Mail>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);
        }

        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Date>(QueryBase::Comparator(QVariantList{QDateTime::fromString("2018-05-22T13:49:41Z", Qt::ISODate), QDateTime::fromString("2018-05-25T13:49:41Z", Qt::ISODate)}, QueryBase::Comparator::Within));
            auto model = Sink::Store::loadModel<Mail>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 2);
        }

        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Date>(QueryBase::Comparator(QVariantList{QDateTime::fromString("2018-05-22T13:49:41Z", Qt::ISODate), QDateTime::fromString("2018-05-30T13:49:41Z", Qt::ISODate)}, QueryBase::Comparator::Within));
            auto model = Sink::Store::loadModel<Mail>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 3);
        }

        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Mail::Date>(QueryBase::Comparator(QVariantList{QDateTime::fromString("2018-05-22T13:49:41Z", Qt::ISODate), QDateTime::fromString("2080-05-30T13:49:41Z", Qt::ISODate)}, QueryBase::Comparator::Within));
            auto model = Sink::Store::loadModel<Mail>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            //This query also finds the mail without date, because we assign a default date of current utc
            QCOMPARE(model->rowCount(), 5);
        }
    }

    void testOverlap()
    {
        auto createEvent = [] (const QString &start, const QString &end) {
            auto icalEvent = KCalCore::Event::Ptr::create();
            icalEvent->setSummary("test");
            icalEvent->setDtStart(QDateTime::fromString(start, Qt::ISODate));
            icalEvent->setDtEnd(QDateTime::fromString(end, Qt::ISODate));

            Event event("sink.dummy.instance1");
            event.setIcal(KCalCore::ICalFormat().toICalString(icalEvent).toUtf8());
            VERIFYEXEC(Sink::Store::create(event));
        };

        createEvent("2018-05-23T12:00:00Z", "2018-05-23T13:00:00Z");
        createEvent("2018-05-23T13:00:00Z", "2018-05-23T14:00:00Z");
        createEvent("2018-05-23T14:00:00Z", "2018-05-23T15:00:00Z");
        createEvent("2018-05-24T12:00:00Z", "2018-05-24T14:00:00Z");
        //Long event that spans multiple buckets
        createEvent("2018-05-30T22:00:00",  "2019-04-25T03:00:00");
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        auto findInRange = [] (const QString &start, const QString &end) {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Event::StartTime, Event::EndTime>(QueryBase::Comparator(
                QVariantList{ QDateTime::fromString(start, Qt::ISODate),
                    QDateTime::fromString(end, Qt::ISODate) },
                QueryBase::Comparator::Overlap));
            return Sink::Store::read<Event>(query);
        };

        //Find all
        QCOMPARE(findInRange("2018-05-22T12:00:00Z", "2018-05-30T13:00:00Z").size(), 4);
        //Find none on day without events
        QCOMPARE(findInRange("2018-05-22T12:00:00Z", "2018-05-22T13:00:00Z").size(), 0);
        //Find none on day with events
        QCOMPARE(findInRange("2018-05-24T10:00:00Z", "2018-05-24T11:00:00Z").size(), 0);
        //Find on same day
        QCOMPARE(findInRange("2018-05-23T12:30:00Z", "2018-05-23T12:31:00Z").size(), 1);
        //Find on different days
        QCOMPARE(findInRange("2018-05-22T12:30:00Z", "2018-05-23T12:00:00Z").size(), 1);
        QCOMPARE(findInRange("2018-05-23T14:30:00Z", "2018-05-23T16:00:00Z").size(), 1);

        //Find long range event
        QCOMPARE(findInRange("2018-07-23T14:30:00Z", "2018-10-23T16:00:00Z").size(), 1);
    }

    void testOverlapLive()
    {
        auto createEvent = [] (const QString &start, const QString &end) {
            auto icalEvent = KCalCore::Event::Ptr::create();
            icalEvent->setSummary("test");
            icalEvent->setDtStart(QDateTime::fromString(start, Qt::ISODate));
            icalEvent->setDtEnd(QDateTime::fromString(end, Qt::ISODate));

            Event event = Event::createEntity<Event>("sink.dummy.instance1");
            event.setIcal(KCalCore::ICalFormat().toICalString(icalEvent).toUtf8());
            VERIFYEXEC_RET(Sink::Store::create(event), {});
            return event;
        };

        createEvent("2018-05-23T12:00:00Z", "2018-05-23T13:00:00Z");
        createEvent("2018-05-23T13:00:00Z", "2018-05-23T14:00:00Z");
        createEvent("2018-05-23T14:00:00Z", "2018-05-23T15:00:00Z");
        createEvent("2018-05-24T12:00:00Z", "2018-05-24T14:00:00Z");
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.setFlags(Query::LiveQuery);
            query.filter<Event::StartTime, Event::EndTime>(QueryBase::Comparator(
                QVariantList{ QDateTime::fromString("2018-05-22T12:00:00Z", Qt::ISODate),
                    QDateTime::fromString("2018-05-30T13:00:00Z", Qt::ISODate) },
                QueryBase::Comparator::Overlap));
            auto model = Sink::Store::loadModel<Event>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 4);

            auto event1 = createEvent("2018-05-23T12:00:00Z", "2018-05-23T13:00:00Z");
            auto event2 = createEvent("2018-05-31T12:00:00Z", "2018-05-31T13:00:00Z");

            QTRY_COMPARE(model->rowCount(), 5);

            VERIFYEXEC(Sink::Store::remove(event1));
            VERIFYEXEC(Sink::Store::remove(event2));

            QTRY_COMPARE(model->rowCount(), 4);
        }
    }

    void testRecurringEvents()
    {
        auto icalEvent = KCalCore::Event::Ptr::create();
        icalEvent->setSummary("test");
        icalEvent->setDtStart(QDateTime::fromString("2018-05-10T13:00:00Z", Qt::ISODate));
        icalEvent->setDtEnd(QDateTime::fromString("2018-05-10T14:00:00Z", Qt::ISODate));
        icalEvent->recurrence()->setWeekly(3);

        Event event = Event::createEntity<Event>("sink.dummy.instance1");
        event.setIcal(KCalCore::ICalFormat().toICalString(icalEvent).toUtf8());
        VERIFYEXEC(Sink::Store::create(event));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");
        query.setFlags(Query::LiveQuery);
        query.filter<Event::StartTime, Event::EndTime>(QueryBase::Comparator(
            QVariantList{ QDateTime::fromString("2018-05-15T12:00:00Z", Qt::ISODate),
                QDateTime::fromString("2018-05-30T13:00:00Z", Qt::ISODate) },
            QueryBase::Comparator::Overlap));
        auto model = Sink::Store::loadModel<Event>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);

        VERIFYEXEC(Sink::Store::remove(event));
        QTRY_COMPARE(model->rowCount(), 0);
    }

    void testRecurringEventsWithExceptions()
    {
        {
            auto icalEvent = KCalCore::Event::Ptr::create();
            icalEvent->setSummary("test");
            icalEvent->setDtStart(QDateTime::fromString("2018-05-10T13:00:00Z", Qt::ISODate));
            icalEvent->setDtEnd(QDateTime::fromString("2018-05-10T14:00:00Z", Qt::ISODate));
            icalEvent->recurrence()->setWeekly(3);

            Event event = Event::createEntity<Event>("sink.dummy.instance1");
            event.setIcal(KCalCore::ICalFormat().toICalString(icalEvent).toUtf8());
            VERIFYEXEC(Sink::Store::create(event));
        }


        //Exception
        {
            auto icalEvent = KCalCore::Event::Ptr::create();
            icalEvent->setSummary("test");
            icalEvent->setRecurrenceId(QDateTime::fromString("2018-05-17T13:00:00Z", Qt::ISODate));
            icalEvent->setDtStart(QDateTime::fromString("2018-07-10T13:00:00Z", Qt::ISODate));
            icalEvent->setDtEnd(QDateTime::fromString("2018-07-10T14:00:00Z", Qt::ISODate));

            Event event = Event::createEntity<Event>("sink.dummy.instance1");
            event.setIcal(KCalCore::ICalFormat().toICalString(icalEvent).toUtf8());
            VERIFYEXEC(Sink::Store::create(event));
        }

        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue("sink.dummy.instance1"));

        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Event::StartTime, Event::EndTime>(QueryBase::Comparator(
                QVariantList{ QDateTime::fromString("2018-05-15T12:00:00Z", Qt::ISODate),
                    QDateTime::fromString("2018-05-30T13:00:00Z", Qt::ISODate) },
                QueryBase::Comparator::Overlap));
            auto model = Sink::Store::loadModel<Event>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 2);
        }
        {
            Sink::Query query;
            query.resourceFilter("sink.dummy.instance1");
            query.filter<Event::StartTime, Event::EndTime>(QueryBase::Comparator(
                QVariantList{ QDateTime::fromString("2018-07-15T12:00:00Z", Qt::ISODate),
                    QDateTime::fromString("2018-07-30T13:00:00Z", Qt::ISODate) },
                QueryBase::Comparator::Overlap));
            auto model = Sink::Store::loadModel<Event>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);
        }
    }


    void testQueryUpdate()
    {
        // Setup
        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedMessageId("test1");
            mail.setFolder("folder1");
            VERIFYEXEC(Sink::Store::create<Mail>(mail));
        }
        {
            Mail mail("sink.dummy.instance1");
            mail.setExtractedMessageId("test2");
            mail.setFolder("folder2");
            VERIFYEXEC(Sink::Store::create<Mail>(mail));
        }

        // Test
        Sink::Query query;
        query.resourceFilter("sink.dummy.instance1");
        query.setFlags(Query::LiveQuery);
        query.filter<Mail::Folder>("folder1");

        auto model = Sink::Store::loadModel<Mail>(query);
        QTRY_COMPARE(model->rowCount(), 1);

        {
            Sink::Query newQuery;
            newQuery.resourceFilter("sink.dummy.instance1");

            Sink::Store::updateModel<Mail>(newQuery, model);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 2);
        }
        {

            Sink::Query newQuery;
            newQuery.resourceFilter("sink.dummy.instance1");
            newQuery.filter<Mail::Folder>("folder2");

            Sink::Store::updateModel<Mail>(newQuery, model);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);
            QCOMPARE(model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>()->getMessageId(), "test2");
        }
        {

            Sink::Query newQuery;
            newQuery.resourceFilter("sink.dummy.instance1");
            newQuery.filter<Mail::Folder>("folder1");

            Sink::Store::updateModel<Mail>(newQuery, model);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);
            QCOMPARE(model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>()->getMessageId(), "test1");
        }
        //Quickly run two queries without waiting for the first to complete.
        {
            {

                Sink::Query newQuery;
                newQuery.resourceFilter("sink.dummy.instance1");
                newQuery.filter<Mail::Folder>("folder2");

                Sink::Store::updateModel<Mail>(newQuery, model);
            }

            Sink::Query newQuery;
            newQuery.resourceFilter("sink.dummy.instance1");
            newQuery.filter<Mail::Folder>("folder1");

            Sink::Store::updateModel<Mail>(newQuery, model);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);
            QCOMPARE(model->data(model->index(0, 0, QModelIndex{}), Sink::Store::DomainObjectRole).value<Mail::Ptr>()->getMessageId(), "test1");
        }
    }

};

QTEST_MAIN(QueryTest)
#include "querytest.moc"
