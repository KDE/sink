#include <QtTest>
#include <QDebug>
#include <functional>

#include "store.h"
#include "facade.h"
#include "resourceconfig.h"
#include "modelresult.h"
#include "resultprovider.h"
#include "facadefactory.h"

/**
 * Test of the resource configuration.
 */
class ResourceConfigTest : public QObject
{
    Q_OBJECT
private slots:

    void initTestCase()
    {
        Sink::FacadeFactory::instance().resetFactory();
        ResourceConfig::clear();
        Sink::Log::setDebugOutputLevel(Sink::Log::Trace);
    }

    void resourceManagement()
    {
        ResourceConfig::clear();
        Sink::FacadeFactory::instance().registerStaticFacades();

        Sink::ApplicationDomain::SinkResource res("", "dummyresource.identifier1", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        res.setProperty("identifier", "dummyresource.identifier1");
        res.setProperty("type", "dummyresource");

        Sink::Store::create(res).exec().waitForFinished();
        {
            Sink::Query query;
            query.propertyFilter.insert("type", Sink::Query::Comparator("dummyresource"));
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::SinkResource>(query);
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        }

        Sink::Store::remove(res).exec().waitForFinished();
        {
            Sink::Query query;
            query.propertyFilter.insert("type", Sink::Query::Comparator("dummyresource"));
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::SinkResource>(query);
            QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
            QCOMPARE(model->rowCount(QModelIndex()), 0);
        }
    }

    void testLoadResourceByCapabiity()
    {
        ResourceConfig::clear();
        Sink::FacadeFactory::instance().registerStaticFacades();

        Sink::ApplicationDomain::SinkResource res("", "dummyresource.identifier1", 0, QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
        res.setProperty("identifier", "dummyresource.identifier1");
        res.setProperty("type", "dummyresource");
        res.setProperty("capabilities", QVariant::fromValue(QByteArrayList() << "foo"));

        Sink::Store::create(res).exec().waitForFinished();
        {
            Sink::Query query;
            query.propertyFilter.insert("type", Sink::Query::Comparator("dummyresource"));
            auto model = Sink::Store::loadModel<Sink::ApplicationDomain::SinkResource>(Sink::Query::CapabilityFilter("foo"));
            QTRY_COMPARE(model->rowCount(QModelIndex()), 1);
        }

        Sink::Store::remove(res).exec().waitForFinished();
    }

};

QTEST_MAIN(ResourceConfigTest)
#include "resourceconfigtest.moc"
