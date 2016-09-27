#include <QtTest>
#include <QDebug>
#include <functional>

#include "store.h"
#include "facade.h"
#include "resourceconfig.h"
#include "modelresult.h"
#include "resultprovider.h"
#include "facadefactory.h"
#include "test.h"
#include "testutils.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

/**
 * Test of the resource configuration.
 */
class ResourceConfigTest : public QObject
{
    Q_OBJECT
private slots:

    void initTestCase()
    {
        Sink::Test::initTest();
        Sink::FacadeFactory::instance().resetFactory();
        ResourceConfig::clear();
    }

    void resourceManagement()
    {
        ResourceConfig::clear();
        Sink::FacadeFactory::instance().registerStaticFacades();

        SinkResource res("", "dummyresource.identifier1", 0, QSharedPointer<MemoryBufferAdaptor>::create());
        res.setProperty("identifier", "dummyresource.identifier1");
        res.setProperty(SinkResource::ResourceType::name, "dummyresource");

        Sink::Store::create(res).exec().waitForFinished();
        {
            Sink::Query query;
            query.filter<SinkResource::ResourceType>("dummyresource");
            auto model = Sink::Store::loadModel<SinkResource>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        }

        Sink::Store::remove(res).exec().waitForFinished();
        {
            Sink::Query query;
            query.filter<SinkResource::ResourceType>("dummyresource");
            auto model = Sink::Store::loadModel<SinkResource>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(QModelIndex()), 0);
        }
    }

    void testLoadResourceByCapability()
    {
        ResourceConfig::clear();
        Sink::FacadeFactory::instance().registerStaticFacades();

        SinkResource res("", "dummyresource.identifier1", 0, QSharedPointer<MemoryBufferAdaptor>::create());
        res.setProperty("identifier", "dummyresource.identifier1");
        res.setResourceType("dummyresource");
        res.setCapabilities(QByteArrayList() << "foo");

        Sink::Store::create(res).exec().waitForFinished();
        {
            Sink::Query query;
            query.filter<SinkResource::ResourceType>("dummyresource");
            auto model = Sink::Store::loadModel<SinkResource>(Query().containsFilter<SinkResource::Capabilities>("foo"));
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        }

        Sink::Store::remove(res).exec().waitForFinished();
    }

    void testLoadResourceStatus()
    {
        ResourceConfig::clear();
        Sink::FacadeFactory::instance().registerStaticFacades();

        auto res = DummyResource::create("");
        VERIFYEXEC(Sink::Store::create(res));
        {
            Sink::Query query;
            query.liveQuery = true;
            query.request<SinkResource::Status>();
            auto model = Sink::Store::loadModel<SinkResource>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
            auto resource = model->data(model->index(0, 0, QModelIndex()), Sink::Store::DomainObjectRole).value<SinkResource::Ptr>();
            QCOMPARE(resource->getStatus(), static_cast<int>(OfflineStatus));

            //Synchronize to connect
            VERIFYEXEC(Sink::Store::synchronize(query));
            QTRY_COMPARE(model->data(model->index(0, 0, QModelIndex()), Sink::Store::DomainObjectRole).value<SinkResource::Ptr>()->getStatus(), static_cast<int>(ConnectedStatus));
        }

        VERIFYEXEC(Sink::Store::remove(res));
    }

};

QTEST_MAIN(ResourceConfigTest)
#include "resourceconfigtest.moc"
