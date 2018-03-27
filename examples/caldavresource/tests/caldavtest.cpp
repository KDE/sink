#include <QtTest>

#include "../caldavresource.h"

#include "common/resourcecontrol.h"
#include "common/secretstore.h"
#include "common/store.h"
#include "common/test.h"
#include "tests/testutils.h"

using Sink::ApplicationDomain::Calendar;
using Sink::ApplicationDomain::DummyResource;
using Sink::ApplicationDomain::Event;
using Sink::ApplicationDomain::SinkResource;

class CalDavTest : public QObject
{
    Q_OBJECT

    SinkResource createResource()
    {
        auto resource = Sink::ApplicationDomain::CalDavResource::create("account1");
        resource.setProperty("server", "http://localhost/dav/calendars/users/doe");
        resource.setProperty("username", "doe");
        Sink::SecretStore::instance().insert(resource.identifier(), "doe");
        resource.setProperty("testmode", true);
        return resource;
    }


    QByteArray mResourceInstanceIdentifier;
    QByteArray mStorageResource;

private slots:

    void initTestCase()
    {
        Sink::Test::initTest();
        auto resource = createResource();
        QVERIFY(!resource.identifier().isEmpty());
        VERIFYEXEC(Sink::Store::create(resource));
        mResourceInstanceIdentifier = resource.identifier();

        auto dummyResource = DummyResource::create("account1");
        VERIFYEXEC(Sink::Store::create(dummyResource));
        mStorageResource = dummyResource.identifier();
        QVERIFY(!mStorageResource.isEmpty());
    }

    void cleanup()
    {
        VERIFYEXEC(Sink::Store::removeDataFromDisk(mResourceInstanceIdentifier));
        VERIFYEXEC(Sink::Store::removeDataFromDisk(mStorageResource));
    }

    void init()
    {
        VERIFYEXEC(Sink::ResourceControl::start(mResourceInstanceIdentifier));
    }

    void testSyncCal()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        // Check in the logs that it doesn't synchronize events again because same CTag
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
    }

    void testSyncCalEmpty()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));

        auto eventJob =
            Sink::Store::fetchAll<Event>(Sink::Query().request<Event::Uid>()).then([](const QList<Event::Ptr> &events) {
                QCOMPARE(events.size(), 14);
            });
        VERIFYEXEC(eventJob);

        auto calendarJob =
            Sink::Store::fetchAll<Calendar>(Sink::Query().request<Calendar::Name>()).then([](const QList<Calendar::Ptr> &calendars) {
                QCOMPARE(calendars.size(), 2);
                for (const auto &calendar : calendars) {
                    QVERIFY(calendar->getName() == "Calendar" || calendar->getName() == "Tasks");
                }
            });
        VERIFYEXEC(calendarJob);

        SinkLog() << "Finished";
    }
};

QTEST_MAIN(CalDavTest)

#include "caldavtest.moc"
