#include <QtTest>

#include <QString>

#include "dummyresource/resourcefactory.h"
#include "clientapi.h"
#include "resourceconfig.h"
#include "log.h"

/**
 * Test of inspection system using the dummy resource.
 *
 * This test requires the dummy resource installed.
 */
class InspectionTest : public QObject
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

    void testInspection_data()
    {
        QTest::addColumn<bool>("success");
        QTest::newRow("success") << true;
        QTest::newRow("fail") << false;
    }

    void testInspection()
    {
        QFETCH(bool, success);
        using namespace Akonadi2;
        using namespace Akonadi2::ApplicationDomain;

        Mail mail(QByteArray("org.kde.dummy.instance1"), QByteArray("identifier"), 0, QSharedPointer<MemoryBufferAdaptor::MemoryBufferAdaptor>::create());

        //testInspection is a magic property that the dummyresource supports
        auto inspectionCommand = Resources::Inspection::PropertyInspection(mail, "testInspection", success);
        auto result = Resources::inspect<Mail>(inspectionCommand).exec();
        result.waitForFinished();
        if (success) {
            QVERIFY(!result.errorCode());
        } else {
            QVERIFY(result.errorCode());
        }
    }
};

QTEST_MAIN(InspectionTest)
#include "inspectiontest.moc"
