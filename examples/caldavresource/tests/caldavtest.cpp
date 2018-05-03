#include <QtTest>

#include <KDAV2/DavCollectionsFetchJob>
#include <KDAV2/DavItemFetchJob>
#include <KDAV2/DavItemModifyJob>
#include <KDAV2/DavItemsListJob>
#include <KDAV2/EtagCache>

#include <KCalCore/Event>
#include <KCalCore/ICalFormat>

#include "../caldavresource.h"

#include "common/resourcecontrol.h"
#include "common/secretstore.h"
#include "common/store.h"
#include "common/test.h"
#include "tests/testutils.h"

#include <algorithm>

using Sink::ApplicationDomain::Calendar;
using Sink::ApplicationDomain::DummyResource;
using Sink::ApplicationDomain::Event;
using Sink::ApplicationDomain::SinkResource;

class CalDavTest : public QObject
{
    Q_OBJECT

    // This test assumes a calendar MyCalendar with one event in it.

    const QString baseUrl = "http://localhost/dav/calendars/users/doe";
    const QString username = "doe";
    const QString password = "doe";

    SinkResource createResource()
    {
        auto resource = Sink::ApplicationDomain::CalDavResource::create("account1");
        resource.setProperty("server", baseUrl);
        resource.setProperty("username", username);
        Sink::SecretStore::instance().insert(resource.identifier(), password);
        resource.setProperty("testmode", true);
        return resource;
    }

    QByteArray mResourceInstanceIdentifier;

    QString addedEventUid;

private slots:

    void initTestCase()
    {
        Sink::Test::initTest();
        auto resource = createResource();
        QVERIFY(!resource.identifier().isEmpty());
        VERIFYEXEC(Sink::Store::create(resource));
        mResourceInstanceIdentifier = resource.identifier();
    }

    void cleanup()
    {
        VERIFYEXEC(Sink::Store::removeDataFromDisk(mResourceInstanceIdentifier));
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
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto eventJob = Sink::Store::fetchAll<Event>(Sink::Query().request<Event::Uid>())
                            .then([](const QList<Event::Ptr> &events) { QCOMPARE(events.size(), 1); });
        VERIFYEXEC(eventJob);

        auto calendarJob = Sink::Store::fetchAll<Calendar>(Sink::Query().request<Calendar::Name>())
                               .then([](const QList<Calendar::Ptr> &calendars) {
                                   QCOMPARE(calendars.size(), 1);
                                   for (const auto &calendar : calendars) {
                                       QVERIFY(calendar->getName() == "MyCalendar");
                                   }
                               });
        VERIFYEXEC(calendarJob);

        SinkLog() << "Finished";
    }

    void testAddEvent()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto job = Sink::Store::fetchOne<Calendar>({}).exec();
        job.waitForFinished();
        QVERIFY2(!job.errorCode(), "Fetching Calendar failed");
        auto calendar = job.value();

        auto event = QSharedPointer<KCalCore::Event>::create();
        event->setSummary("Hello");
        event->setDtStart(QDateTime::currentDateTime());
        event->setDtEnd(QDateTime::currentDateTime().addSecs(3600));
        event->setCreated(QDateTime::currentDateTime());
        addedEventUid = QUuid::createUuid().toString();
        event->setUid(addedEventUid);

        auto ical = KCalCore::ICalFormat().toICalString(event);
        Event sinkEvent(mResourceInstanceIdentifier);
        sinkEvent.setIcal(ical.toUtf8());
        sinkEvent.setCalendar(calendar);

        SinkLog() << "Adding event";
        VERIFYEXEC(Sink::Store::create(sinkEvent));
        VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

        auto verifyEventCountJob =
            Sink::Store::fetchAll<Event>(Sink::Query().request<Event::Uid>()).then([](const QList<Event::Ptr> &events) {
                QCOMPARE(events.size(), 2);
            });
        VERIFYEXEC(verifyEventCountJob);

        auto verifyEventJob =
            Sink::Store::fetchOne<Event>(Sink::Query().filter("uid", Sink::Query::Comparator(addedEventUid)))
                .then([](const Event &event) { QCOMPARE(event.getSummary(), {"Hello"}); });
        VERIFYEXEC(verifyEventJob);
    }

    void testModifyEvent()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto job = Sink::Store::fetchOne<Event>(
            Sink::Query().filter("uid", Sink::Query::Comparator(addedEventUid)))
                       .exec();
        job.waitForFinished();
        QVERIFY2(!job.errorCode(), "Fetching Event failed");
        auto event = job.value();

        auto incidence = KCalCore::ICalFormat().readIncidence(event.getIcal());
        auto calevent = incidence.dynamicCast<KCalCore::Event>();
        QVERIFY2(calevent, "Cannot convert to KCalCore event");

        calevent->setSummary("Hello World!");
        auto dummy = QSharedPointer<KCalCore::Event>(calevent);
        auto newical = KCalCore::ICalFormat().toICalString(dummy);

        event.setIcal(newical.toUtf8());

        VERIFYEXEC(Sink::Store::modify(event));

        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto verifyEventCountJob = Sink::Store::fetchAll<Event>({}).then(
            [](const QList<Event::Ptr> &events) { QCOMPARE(events.size(), 2); });
        VERIFYEXEC(verifyEventCountJob);

        auto verifyEventJob =
            Sink::Store::fetchOne<Event>(Sink::Query().filter("uid", Sink::Query::Comparator(addedEventUid)))
                .then([](const Event &event) { QCOMPARE(event.getSummary(), {"Hello World!"}); });
        VERIFYEXEC(verifyEventJob);
    }

    void testSneakyModifyEvent()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        // Change the item without sink's knowledge
        {
            qWarning() << 1;
            auto collection = ([this]() -> KDAV2::DavCollection {
                QUrl url(baseUrl);
                url.setUserName(username);
                url.setPassword(password);
                KDAV2::DavUrl davurl(url, KDAV2::CalDav);
                KDAV2::DavCollectionsFetchJob collectionsJob(davurl);
                collectionsJob.exec();
                Q_ASSERT(collectionsJob.error() == 0);
                return collectionsJob.collections()[0];
            })();

            auto itemList = ([&collection]() -> KDAV2::DavItem::List {
                auto cache = std::make_shared<KDAV2::EtagCache>();
                KDAV2::DavItemsListJob itemsListJob(collection.url(), cache);
                itemsListJob.exec();
                Q_ASSERT(itemsListJob.error() == 0);
                return itemsListJob.items();
            })();
            auto hollowDavItemIt =
                std::find_if(itemList.begin(), itemList.end(), [this](const KDAV2::DavItem &item) {
                    return item.url().url().path().endsWith(addedEventUid);
                });

            auto davitem = ([this, &collection, &hollowDavItemIt]() -> KDAV2::DavItem {
                QString itemUrl = collection.url().url().toEncoded() + addedEventUid;
                KDAV2::DavItemFetchJob itemFetchJob(*hollowDavItemIt);
                itemFetchJob.exec();
                Q_ASSERT(itemFetchJob.error() == 0);
                return itemFetchJob.item();
            })();

            qWarning() << 3;
            auto incidence = KCalCore::ICalFormat().readIncidence(davitem.data());
            auto calevent = incidence.dynamicCast<KCalCore::Event>();
            QVERIFY2(calevent, "Cannot convert to KCalCore event");

            qWarning() << 4;
            calevent->setSummary("Manual Hello World!");
            auto newical = KCalCore::ICalFormat().toICalString(calevent);

            qWarning() << 5;
            davitem.setData(newical.toUtf8());
            KDAV2::DavItemModifyJob itemModifyJob(davitem);
            itemModifyJob.exec();
            QVERIFY2(itemModifyJob.error() == 0, "Cannot modify item");

            qWarning() << 6;
        }

        // Try to change the item with sink
        {
            auto job = Sink::Store::fetchOne<Event>(
                Sink::Query().filter("uid", Sink::Query::Comparator(addedEventUid)))
                           .exec();
            job.waitForFinished();
            QVERIFY2(!job.errorCode(), "Fetching Event failed");
            auto event = job.value();

            auto incidence = KCalCore::ICalFormat().readIncidence(event.getIcal());
            auto calevent = incidence.dynamicCast<KCalCore::Event>();
            QVERIFY2(calevent, "Cannot convert to KCalCore event");

            calevent->setSummary("Sink Hello World!");
            auto dummy = QSharedPointer<KCalCore::Event>(calevent);
            auto newical = KCalCore::ICalFormat().toICalString(dummy);

            event.setIcal(newical.toUtf8());

            // TODO: make that fail
            VERIFYEXEC(Sink::Store::modify(event));
            VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));
        }
    }

    void testRemoveEvent()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto job = Sink::Store::fetchOne<Event>(
            Sink::Query().filter("uid", Sink::Query::Comparator(addedEventUid)))
                       .exec();
        job.waitForFinished();
        QVERIFY2(!job.errorCode(), "Fetching Event failed");
        auto event = job.value();

        VERIFYEXEC(Sink::Store::remove(event));
        VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

        auto verifyEventCountJob = Sink::Store::fetchAll<Event>({}).then(
            [](const QList<Event::Ptr> &events) { QCOMPARE(events.size(), 1); });
        VERIFYEXEC(verifyEventCountJob);
    }
};

QTEST_MAIN(CalDavTest)

#include "caldavtest.moc"
