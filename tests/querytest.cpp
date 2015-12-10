#include <QtTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "clientapi.h"
#include "commands.h"
#include "resourceconfig.h"
#include "log.h"
#include "modelresult.h"

/**
 * Test of the query system using the dummy resource.
 *
 * This test requires the dummy resource installed.
 */
class QueryTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        Akonadi2::Log::setDebugOutputLevel(Akonadi2::Log::Trace);
        auto factory = Akonadi2::ResourceFactory::load("org.kde.dummy");
        QVERIFY(factory);
        DummyResource::removeFromDisk("org.kde.dummy.instance1");
        ResourceConfig::addResource("org.kde.dummy.instance1", "org.kde.dummy");
    }

    void cleanup()
    {
        Akonadi2::Store::shutdown(QByteArray("org.kde.dummy.instance1")).exec().waitForFinished();
        DummyResource::removeFromDisk("org.kde.dummy.instance1");
        auto factory = Akonadi2::ResourceFactory::load("org.kde.dummy");
        QVERIFY(factory);
        Akonadi2::Store::start(QByteArray("org.kde.dummy.instance1")).exec().waitForFinished();
    }

    void init()
    {
        qDebug();
        qDebug() << "-----------------------------------------";
        qDebug();
    }

    void testNoResources()
    {
        //Test
        Akonadi2::Query query;
        query.resources << "foobar";
        query.syncOnDemand = false;
        query.processAll = false;
        query.liveQuery = true;

        //We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 0);
    }


    void testSingle()
    {
        //Setup
        {
            Akonadi2::ApplicationDomain::Mail mail("org.kde.dummy.instance1");
            Akonadi2::Store::create<Akonadi2::ApplicationDomain::Mail>(mail).exec().waitForFinished();
        }

        //Test
        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = false;
        query.liveQuery = true;

        //We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Mail>(query);
        model->fetchMore(QModelIndex());
        QTRY_COMPARE(model->rowCount(), 1);
    }

    void testSingleWithDelay()
    {
        //Setup
        {
            Akonadi2::ApplicationDomain::Mail mail("org.kde.dummy.instance1");
            Akonadi2::Store::create<Akonadi2::ApplicationDomain::Mail>(mail).exec().waitForFinished();
        }

        //Test
        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;
        query.liveQuery = false;

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        //We fetch after the data is available and don't rely on the live query mechanism to deliver the actual data
        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Mail>(query);

        model->fetchMore(QModelIndex());
        QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
    }

    void testFolder()
    {
        //Setup
        {
            Akonadi2::ApplicationDomain::Folder folder("org.kde.dummy.instance1");
            Akonadi2::Store::create<Akonadi2::ApplicationDomain::Folder>(folder).exec().waitForFinished();
        }

        //Test
        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = false;
        query.liveQuery = true;

        //We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
        model->fetchMore(QModelIndex());
        QTRY_COMPARE(model->rowCount(), 1);
        auto folderEntity = model->index(0, 0).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Folder::Ptr>();
        QVERIFY(!folderEntity->identifier().isEmpty());
    }

    void testFolderTree()
    {
        //Setup
        {
            Akonadi2::ApplicationDomain::Folder folder("org.kde.dummy.instance1");
            Akonadi2::Store::create<Akonadi2::ApplicationDomain::Folder>(folder).exec().waitForFinished();

            Akonadi2::Query query;
            query.resources << "org.kde.dummy.instance1";
            query.syncOnDemand = false;
            query.processAll = true;

            //Ensure all local data is processed
            Akonadi2::Store::synchronize(query).exec().waitForFinished();

            auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);

            auto folderEntity = model->index(0, 0).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Folder::Ptr>();
            QVERIFY(!folderEntity->identifier().isEmpty());

            Akonadi2::ApplicationDomain::Folder subfolder("org.kde.dummy.instance1");
            subfolder.setProperty("parent", folderEntity->identifier());
            Akonadi2::Store::create<Akonadi2::ApplicationDomain::Folder>(subfolder).exec().waitForFinished();
        }

        //Test
        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;
        query.parentProperty = "parent";

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        //We fetch after the data is available and don't rely on the live query mechanism to deliver the actual data
        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
        model->fetchMore(QModelIndex());
        QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
        model->fetchMore(model->index(0, 0));
        QTRY_VERIFY(model->data(model->index(0, 0), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(model->index(0, 0)), 1);
    }

    void testMailByUid()
    {
        //Setup
        {
            Akonadi2::ApplicationDomain::Mail mail("org.kde.dummy.instance1");
            mail.setProperty("uid", "test1");
            mail.setProperty("sender", "doe@example.org");
            Akonadi2::Store::create<Akonadi2::ApplicationDomain::Mail>(mail).exec().waitForFinished();
        }

        //Test
        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;
        query.liveQuery = false;
        query.propertyFilter.insert("uid", "test1");

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        //We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
    }

    void testMailByFolder()
    {
        //Setup
        Akonadi2::ApplicationDomain::Folder::Ptr folderEntity;
        {
            Akonadi2::ApplicationDomain::Folder folder("org.kde.dummy.instance1");
            Akonadi2::Store::create<Akonadi2::ApplicationDomain::Folder>(folder).exec().waitForFinished();

            Akonadi2::Query query;
            query.resources << "org.kde.dummy.instance1";
            query.syncOnDemand = false;
            query.processAll = true;

            //Ensure all local data is processed
            Akonadi2::Store::synchronize(query).exec().waitForFinished();

            auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Folder>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(), 1);

            folderEntity = model->index(0, 0).data(Akonadi2::Store::DomainObjectRole).value<Akonadi2::ApplicationDomain::Folder::Ptr>();
            QVERIFY(!folderEntity->identifier().isEmpty());

            Akonadi2::ApplicationDomain::Mail mail("org.kde.dummy.instance1");
            mail.setProperty("uid", "test1");
            mail.setProperty("folder", folderEntity->identifier());
            Akonadi2::Store::create<Akonadi2::ApplicationDomain::Mail>(mail).exec().waitForFinished();
        }

        //Test
        Akonadi2::Query query;
        query.resources << "org.kde.dummy.instance1";
        query.syncOnDemand = false;
        query.processAll = true;
        query.liveQuery = false;
        query.propertyFilter.insert("folder", folderEntity->identifier());

        //Ensure all local data is processed
        Akonadi2::Store::synchronize(query).exec().waitForFinished();

        //We fetch before the data is available and rely on the live query mechanism to deliver the actual data
        auto model = Akonadi2::Store::loadModel<Akonadi2::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Akonadi2::Store::ChildrenFetchedRole).toBool());
        QCOMPARE(model->rowCount(), 1);
    }
};

QTEST_MAIN(QueryTest)
#include "querytest.moc"
