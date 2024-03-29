#include <QTest>

#include <KDAV2/DavCollectionsFetchJob>
#include <KDAV2/DavCollectionCreateJob>
#include <KDAV2/DavCollectionDeleteJob>
#include <KDAV2/DavItemFetchJob>
#include <KDAV2/DavItemModifyJob>
#include <KDAV2/DavItemCreateJob>
#include <KDAV2/DavItemsListJob>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>

#include "../caldavresource.h"

#include "common/resourcecontrol.h"
#include "common/secretstore.h"
#include "common/store.h"
#include "common/test.h"
#include "common/query.h"

#include <algorithm>

using namespace Sink;
using Sink::ApplicationDomain::Calendar;
using Sink::ApplicationDomain::DummyResource;
using Sink::ApplicationDomain::Event;
using Sink::ApplicationDomain::Todo;
using Sink::ApplicationDomain::SinkResource;

class CalDavTest : public QObject
{
    Q_OBJECT

    // This test assumes a calendar "personal".

    const QString baseUrl = "http://localhost/dav/calendars/user/doe";
    const QString username = "doe";
    const QString password = "doe";

    SinkResource createResource()
    {
        auto resource = Sink::ApplicationDomain::CalDavResource::create("account1");
        resource.setProperty("server", "http://localhost");
        resource.setProperty("username", username);
        Sink::SecretStore::instance().insert(resource.identifier(), password);
        return resource;
    }

    QByteArray mResourceInstanceIdentifier;

    QByteArray createEvent(const QString &subject, const QString &collectionName)
    {
        const auto collectionUrl = findCollection(collectionName);

        QUrl url{collectionUrl.url().toString() + subject + ".ical"};
        url.setUserName(QStringLiteral("doe"));
        url.setPassword(QStringLiteral("doe"));

        KDAV2::DavUrl testItemUrl(url, KDAV2::CardDav);

        auto event = QSharedPointer<KCalendarCore::Event>::create();
        event->setSummary(subject);
        event->setDtStart(QDateTime::currentDateTime());
        event->setDtEnd(QDateTime::currentDateTime().addSecs(3600));
        event->setCreated(QDateTime::currentDateTime());
        event->setUid(subject);

        auto data = KCalendarCore::ICalFormat().toICalString(event).toUtf8();

        KDAV2::DavItem item(testItemUrl, QStringLiteral("text/calendar"), data, QString());
        auto createJob = new KDAV2::DavItemCreateJob(item);
        createJob->exec();
        if (createJob->error()) {
            qWarning() << createJob->errorString();
        }
        return event->uid().toUtf8();
    }

    void createCollection(const QString &name)
    {
        QUrl mainUrl(QStringLiteral("http://localhost/dav/calendars/user/doe/") + name);
        mainUrl.setUserName(QStringLiteral("doe"));
        mainUrl.setPassword(QStringLiteral("doe"));

        KDAV2::DavUrl davUrl(mainUrl, KDAV2::CalDav);
        KDAV2::DavCollection collection{davUrl, name, KDAV2::DavCollection::Events};

        auto createJob = new KDAV2::DavCollectionCreateJob(collection);
        createJob->exec();
        if (createJob->error()) {
            qWarning() << createJob->errorString();
        }
    }

    KDAV2::DavUrl findCollection(const QString &collectionName)
    {
        QUrl mainUrl{"http://localhost/dav/calendars/user/doe"};
        mainUrl.setUserName(QStringLiteral("doe"));
        mainUrl.setPassword(QStringLiteral("doe"));

        KDAV2::DavUrl davUrl(mainUrl, KDAV2::CalDav);

        auto *job = new KDAV2::DavCollectionsFetchJob(davUrl);
        job->exec();

        const auto collectionUrl = [&] {
            for (const auto &col : job->collections()) {
                // qWarning() << "Looking for " << collectionName << col.displayName();
                if (col.displayName() == collectionName) {
                    return col.url();
                }
            }
            return KDAV2::DavUrl{};
        }();
        return collectionUrl;
    }

    void removeCollection(const QString &collectionName)
    {
        auto deleteJob = new KDAV2::DavCollectionDeleteJob(findCollection(collectionName));
        deleteJob->exec();
        if (deleteJob->error()) {
            qWarning() << deleteJob->errorString();
        }
    }

    int modifyEvent(const QString &eventUid, const QString &newSummary)
    {
        auto collection = [&]() -> KDAV2::DavCollection {
            QUrl url(baseUrl);
            url.setUserName(username);
            url.setPassword(password);
            KDAV2::DavUrl davurl(url, KDAV2::CalDav);
            auto collectionsJob = new KDAV2::DavCollectionsFetchJob(davurl);
            collectionsJob->exec();
            Q_ASSERT(collectionsJob->error() == 0);
            for (const auto &col : collectionsJob->collections()) {
                if (col.displayName() == "personal") {
                    return col;
                }
            }
            return {};
        }();

        auto itemList = ([&collection]() -> KDAV2::DavItem::List {
            auto itemsListJob = new KDAV2::DavItemsListJob(collection.url());
            itemsListJob->exec();
            Q_ASSERT(itemsListJob->error() == 0);
            return itemsListJob->items();
        })();
        auto hollowDavItemIt =
            std::find_if(itemList.begin(), itemList.end(), [&](const KDAV2::DavItem &item) {
                return item.url().url().path().contains(eventUid);
            });
        Q_ASSERT(hollowDavItemIt != itemList.end());

        auto davitem = ([&]() -> KDAV2::DavItem {
            auto itemFetchJob = new KDAV2::DavItemFetchJob(*hollowDavItemIt);
            itemFetchJob->exec();
            Q_ASSERT(itemFetchJob->error() == 0);
            return itemFetchJob->item();
        })();

        auto incidence = KCalendarCore::ICalFormat().readIncidence(davitem.data());
        auto calevent = incidence.dynamicCast<KCalendarCore::Event>();
        Q_ASSERT(calevent);

        calevent->setSummary(newSummary);
        auto newical = KCalendarCore::ICalFormat().toICalString(calevent);

        davitem.setData(newical.toUtf8());
        auto itemModifyJob = new KDAV2::DavItemModifyJob(davitem);
        itemModifyJob->exec();
        return itemModifyJob->error();
    }

    void resetTestEnvironment()
    {
        system("resetcalendar.sh");
    }

private slots:

    void initTestCase()
    {
        Sink::Test::initTest();
        resetTestEnvironment();
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

    void testSyncCalEmpty()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        QCOMPARE(Sink::Store::read<Event>({}).size(), 0);
        QCOMPARE(Sink::Store::read<Todo>({}).size(), 0);

        const auto calendars = Sink::Store::read<Calendar>(Sink::Query().request<Calendar::Name>());
        QCOMPARE(calendars.size(), 1);
        QCOMPARE(calendars.first().getName(), QLatin1String{"personal"});
    }

    void testSyncCalendars()
    {
        createCollection("calendar2");

        Sink::SyncScope scope;
        scope.setType<Calendar>();
        scope.resourceFilter(mResourceInstanceIdentifier);

        VERIFYEXEC(Sink::Store::synchronize(scope));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        const auto calendars = Sink::Store::read<Calendar>(Sink::Query().resourceFilter(mResourceInstanceIdentifier));
        QCOMPARE(calendars.size(), 2);
    }

    void testSyncEvents()
    {
        createEvent("event1", "personal");
        createEvent("event2", "personal");
        createEvent("event3", "calendar2");

        //Get the calendars first because we rely on them for the next query.
        {
            Sink::SyncScope scope;
            scope.setType<Calendar>();
            scope.resourceFilter(mResourceInstanceIdentifier);
            VERIFYEXEC(Sink::Store::synchronize(scope));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        }

        //We explicitly set an empty calendar filter to override the default query for enabled calendars only
        Sink::SyncScope scope;
        scope.setType<Event>();
        Sink::Query q;
        q.setType<Calendar>();
        scope.filter(ApplicationDomain::getTypeName<Calendar>(), {QVariant::fromValue(q)});
        scope.resourceFilter(mResourceInstanceIdentifier);

        VERIFYEXEC(Sink::Store::synchronize(scope));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        const auto events = Sink::Store::read<Event>(Sink::Query().resourceFilter(mResourceInstanceIdentifier));
        QCOMPARE(events.size(), 3);

        //Ensure a resync works
        {
            VERIFYEXEC(Sink::Store::synchronize(scope));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
            const auto events = Sink::Store::read<Event>(Sink::Query().resourceFilter(mResourceInstanceIdentifier));
            QCOMPARE(events.size(), 3);
            for (const auto &event : events) {
                const auto calendars = Sink::Store::read<Calendar>(Sink::Query().resourceFilter(mResourceInstanceIdentifier).filter(event.getCalendar()));
                QCOMPARE(calendars.size(), 1);
            }
        }

        //Ensure a resync after another creation works
        createEvent("event4", "calendar2");
        {
            VERIFYEXEC(Sink::Store::synchronize(scope));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
            const auto events = Sink::Store::read<Event>(Sink::Query().resourceFilter(mResourceInstanceIdentifier));
            QCOMPARE(events.size(), 4);
        }
    }

    void testCreateModifyDeleteEvent()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto calendar = Sink::Store::readOne<Calendar>(Sink::Query{}.filter<Calendar::Name>("personal"));

        auto event = QSharedPointer<KCalendarCore::Event>::create();
        event->setSummary("Hello");
        event->setDtStart(QDateTime::currentDateTime());
        event->setDtEnd(QDateTime::currentDateTime().addSecs(3600));
        event->setCreated(QDateTime::currentDateTime());
        auto addedEventUid = QUuid::createUuid().toString();
        event->setUid(addedEventUid);

        auto ical = KCalendarCore::ICalFormat().toICalString(event);
        Event sinkEvent(mResourceInstanceIdentifier);
        sinkEvent.setIcal(ical.toUtf8());
        sinkEvent.setCalendar(calendar);

        VERIFYEXEC(Sink::Store::create(sinkEvent));
        VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

        auto events = Sink::Store::read<Event>(Sink::Query().filter("uid", Sink::Query::Comparator(addedEventUid)));
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().getSummary(), QLatin1String{"Hello"});
        QCOMPARE(events.first().getCalendar(), calendar.identifier());

        //Modify
        {
            auto event = events.first();
            auto incidence = KCalendarCore::ICalFormat().readIncidence(event.getIcal());
            auto calevent = incidence.dynamicCast<KCalendarCore::Event>();
            QVERIFY2(calevent, "Cannot convert to KCalendarCore event");

            calevent->setSummary("Hello World!");
            auto dummy = QSharedPointer<KCalendarCore::Event>(calevent);
            auto newical = KCalendarCore::ICalFormat().toICalString(dummy);

            event.setIcal(newical.toUtf8());

            VERIFYEXEC(Sink::Store::modify(event));

            VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

            auto events = Sink::Store::read<Event>(Sink::Query().filter("uid", Sink::Query::Comparator(addedEventUid)));
            QCOMPARE(events.size(), 1);
            QCOMPARE(events.first().getSummary(), QLatin1String{"Hello World!"});
        }
        //Delete
        {
            auto event = events.first();

            VERIFYEXEC(Sink::Store::remove(event));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
            VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

            auto events = Sink::Store::read<Event>(Sink::Query().filter("uid", Sink::Query::Comparator(addedEventUid)));
            QCOMPARE(events.size(), 0);
        }
    }

    void testCreateModifyDeleteTodo()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto calendar = Sink::Store::readOne<Calendar>(Sink::Query{}.filter<Calendar::Name>("personal"));

        auto todo = QSharedPointer<KCalendarCore::Todo>::create();
        todo->setSummary("Hello");
        todo->setDtStart(QDateTime::currentDateTime());
        todo->setCreated(QDateTime::currentDateTime());
        auto addedTodoUid = QUuid::createUuid().toString();
        todo->setUid(addedTodoUid);

        auto ical = KCalendarCore::ICalFormat().toICalString(todo);
        Todo sinkTodo(mResourceInstanceIdentifier);
        sinkTodo.setIcal(ical.toUtf8());
        sinkTodo.setCalendar(calendar);

        VERIFYEXEC(Sink::Store::create(sinkTodo));
        VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

        auto todos = Sink::Store::read<Todo>(Sink::Query().filter("uid", Sink::Query::Comparator(addedTodoUid)));
        QCOMPARE(todos.size(), 1);
        QCOMPARE(todos.first().getSummary(), QLatin1String{"Hello"});

        //Modify
        {
            auto todo = todos.first();
            auto incidence = KCalendarCore::ICalFormat().readIncidence(todo.getIcal());
            auto caltodo = incidence.dynamicCast<KCalendarCore::Todo>();
            QVERIFY2(caltodo, "Cannot convert to KCalendarCore todo");

            caltodo->setSummary("Hello World!");
            auto dummy = QSharedPointer<KCalendarCore::Todo>(caltodo);
            auto newical = KCalendarCore::ICalFormat().toICalString(dummy);

            todo.setIcal(newical.toUtf8());

            VERIFYEXEC(Sink::Store::modify(todo));

            VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

            auto todos = Sink::Store::read<Todo>(Sink::Query().filter("uid", Sink::Query::Comparator(addedTodoUid)));
            QCOMPARE(todos.size(), 1);
            QCOMPARE(todos.first().getSummary(), QLatin1String{"Hello World!"});
        }
        //Delete
        {
            auto todo = todos.first();

            VERIFYEXEC(Sink::Store::remove(todo));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
            VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

            auto todos = Sink::Store::read<Todo>(Sink::Query().filter("uid", Sink::Query::Comparator(addedTodoUid)));
            QCOMPARE(todos.size(), 0);
        }
    }

    void testModificationConflict()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto calendar = Sink::Store::readOne<Calendar>(Sink::Query{}.filter<Calendar::Name>("personal"));

        auto event = QSharedPointer<KCalendarCore::Event>::create();
        event->setSummary("Hello");
        event->setDtStart(QDateTime::currentDateTime());
        event->setDtEnd(QDateTime::currentDateTime().addSecs(3600));
        event->setCreated(QDateTime::currentDateTime());
        auto addedEventUid = QUuid::createUuid().toString();
        event->setUid(addedEventUid);

        auto ical = KCalendarCore::ICalFormat().toICalString(event);
        Event sinkEvent(mResourceInstanceIdentifier);
        sinkEvent.setIcal(ical.toUtf8());
        sinkEvent.setCalendar(calendar);

        VERIFYEXEC(Sink::Store::create(sinkEvent));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

        // Change the item without sink's knowledge
        QVERIFY2(modifyEvent(addedEventUid, "Manual Hello World!") == 0, "Cannot modify item");

        //Change the item with sink as well, this will create a conflict
        {
            auto event = Sink::Store::readOne<Event>(Sink::Query().filter("uid", Sink::Query::Comparator(addedEventUid)));
            auto calevent = KCalendarCore::ICalFormat().readIncidence(event.getIcal()).dynamicCast<KCalendarCore::Event>();
            QVERIFY(calevent);

            calevent->setSummary("Sink Hello World!");
            event.setIcal(KCalendarCore::ICalFormat().toICalString(calevent).toUtf8());

            VERIFYEXEC(Sink::Store::modify(event));
            VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

            {
                auto event = Sink::Store::readOne<Event>(Sink::Query().filter("uid", Sink::Query::Comparator(addedEventUid)));
                QCOMPARE(event.getSummary(), QLatin1String{"Sink Hello World!"});
            }
        }

        // Change the item without sink's knowledge again
        QVERIFY2(modifyEvent(addedEventUid, "Manual Hello World2!") == 0, "Cannot modify item");

        //Try to synchronize the modification, the conflict should be resolved by now.
        Sink::SyncScope scope;
        scope.setType<Event>();
        Sink::Query q;
        q.setType<Calendar>();
        scope.filter(ApplicationDomain::getTypeName<Calendar>(), {QVariant::fromValue(q)});
        scope.resourceFilter(mResourceInstanceIdentifier);
        VERIFYEXEC(Sink::Store::synchronize(scope));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        {
            auto event = Sink::Store::readOne<Event>(Sink::Query().filter("uid", Sink::Query::Comparator(addedEventUid)));
            QCOMPARE(event.getSummary(), QLatin1String{"Manual Hello World2!"});
        }
    }


    void testSyncRemoveFullCalendar()
    {
        createCollection("calendar3");
        createEvent("eventToRemove", "calendar3");

        //Get the calendars first because we rely on them for the next query.
        {
            Sink::SyncScope scope;
            scope.setType<Calendar>();
            scope.resourceFilter(mResourceInstanceIdentifier);
            VERIFYEXEC(Sink::Store::synchronize(scope));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        }

        //We explicitly set an empty calendar filter to override the default query for enabled calendars only
        Sink::SyncScope scope;
        scope.setType<Event>();
        Sink::Query q;
        q.setType<Calendar>();
        scope.filter(ApplicationDomain::getTypeName<Calendar>(), {QVariant::fromValue(q)});
        scope.resourceFilter(mResourceInstanceIdentifier);


        VERIFYEXEC(Sink::Store::synchronize(scope));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        QCOMPARE(Sink::Store::read<Calendar>(Sink::Query{}.filter<Calendar::Name>("calendar3")).size(), 1);
        QCOMPARE(Sink::Store::read<Event>(Sink::Query{}.filter<Event::Summary>("eventToRemove")).size(), 1);


        removeCollection("calendar3");

        {
            Sink::SyncScope scope;
            scope.setType<Calendar>();
            scope.resourceFilter(mResourceInstanceIdentifier);
            VERIFYEXEC(Sink::Store::synchronize(scope));
        }
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        QCOMPARE(Sink::Store::read<Calendar>(Sink::Query{}.filter<Calendar::Name>("calendar3")).size(), 0);
        QCOMPARE(Sink::Store::read<Event>(Sink::Query{}.filter<Event::Summary>("eventToRemove")).size(), 0);
    }

    void testSyncRemoveCalendar()
    {
        createCollection("calendar4");
        createEvent("eventToRemove", "calendar4");

        //Get the calendars first because we rely on them for the next query.
        {
            Sink::SyncScope scope;
            scope.setType<Calendar>();
            scope.resourceFilter(mResourceInstanceIdentifier);
            VERIFYEXEC(Sink::Store::synchronize(scope));
            VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        }

        //We explicitly set an empty calendar filter to override the default query for enabled calendars only
        Sink::SyncScope scope;
        scope.setType<Event>();
        Sink::Query q;
        q.setType<Calendar>();
        scope.filter(ApplicationDomain::getTypeName<Calendar>(), {QVariant::fromValue(q)});
        scope.resourceFilter(mResourceInstanceIdentifier);


        VERIFYEXEC(Sink::Store::synchronize(scope));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        const auto list = Sink::Store::read<Calendar>(Sink::Query{}.filter<Calendar::Name>("calendar4"));
        QCOMPARE(list.size(), 1);
        QCOMPARE(Sink::Store::read<Event>(Sink::Query{}.filter<Event::Summary>("eventToRemove")).size(), 1);

        VERIFYEXEC(Sink::Store::remove(list.first()));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

        QCOMPARE(Sink::Store::read<Calendar>(Sink::Query{}.filter<Calendar::Name>("calendar4")).size(), 0);
        QCOMPARE(Sink::Store::read<Event>(Sink::Query{}.filter<Event::Summary>("eventToRemove")).size(), 0);
    }

    void testCreateRemoveCalendar()
    {
        auto calendar = Sink::ApplicationDomain::ApplicationDomainType::createEntity<Sink::ApplicationDomain::Calendar>(mResourceInstanceIdentifier);
        calendar.setName("calendar5");
        VERIFYEXEC(Sink::Store::create(calendar));

        auto event = QSharedPointer<KCalendarCore::Event>::create();
        event->setSummary("eventToRemove");
        event->setDtStart(QDateTime::currentDateTime());
        event->setCreated(QDateTime::currentDateTime());
        event->setUid("eventToRemove");

        auto ical = KCalendarCore::ICalFormat().toICalString(event);
        Event sinkEvent(mResourceInstanceIdentifier);
        sinkEvent.setIcal(ical.toUtf8());
        sinkEvent.setCalendar(calendar);

        VERIFYEXEC(Sink::Store::create(sinkEvent));

        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

        QVERIFY(findCollection("calendar5").url().isValid());

        const auto list = Sink::Store::read<Calendar>(Sink::Query{}.filter<Calendar::Name>("calendar5"));
        QCOMPARE(list.size(), 1);
        QCOMPARE(Sink::Store::read<Event>(Sink::Query{}.filter<Event::Summary>("eventToRemove")).size(), 1);

        VERIFYEXEC(Sink::Store::remove(list.first()));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

        QCOMPARE(Sink::Store::read<Calendar>(Sink::Query{}.filter<Calendar::Name>("calendar5")).size(), 0);
        QCOMPARE(Sink::Store::read<Event>(Sink::Query{}.filter<Event::Summary>("eventToRemove")).size(), 0);


        QVERIFY(!findCollection("calendar5").url().isValid());
    }
};

QTEST_MAIN(CalDavTest)

#include "caldavtest.moc"
