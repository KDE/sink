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
using Sink::ApplicationDomain::Todo;
using Sink::ApplicationDomain::SinkResource;

class CalDavTest : public QObject
{
    Q_OBJECT

    // This test assumes a calendar MyCalendar with one event and one todo in
    // it.

    const QString baseUrl = "http://localhost/dav/calendars/user/doe";
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
    QString addedTodoUid;

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
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
    }

    void testSyncCalEmpty()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto eventJob = Sink::Store::fetchAll<Event>(Sink::Query().request<Event::Uid>())
                            .then([](const QList<Event::Ptr> &events) { QCOMPARE(events.size(), 1); });
        auto todoJob = Sink::Store::fetchAll<Todo>(Sink::Query().request<Todo::Uid>())
                            .then([](const QList<Todo::Ptr> &todos) { QCOMPARE(todos.size(), 1); });

        VERIFYEXEC(eventJob);
        VERIFYEXEC(todoJob);

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

    void testAddTodo()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto job = Sink::Store::fetchOne<Calendar>({}).exec();
        job.waitForFinished();
        QVERIFY2(!job.errorCode(), "Fetching Calendar failed");
        auto calendar = job.value();

        auto todo = QSharedPointer<KCalCore::Todo>::create();
        todo->setSummary("Hello");
        todo->setDtStart(QDateTime::currentDateTime());
        todo->setCreated(QDateTime::currentDateTime());
        addedTodoUid = QUuid::createUuid().toString();
        todo->setUid(addedTodoUid);

        auto ical = KCalCore::ICalFormat().toICalString(todo);
        Todo sinkTodo(mResourceInstanceIdentifier);
        sinkTodo.setIcal(ical.toUtf8());
        sinkTodo.setCalendar(calendar);

        SinkLog() << "Adding todo";
        VERIFYEXEC(Sink::Store::create(sinkTodo));
        VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

        auto verifyTodoCountJob =
            Sink::Store::fetchAll<Todo>(Sink::Query().request<Todo::Uid>()).then([](const QList<Todo::Ptr> &todos) {
                QCOMPARE(todos.size(), 2);
            });
        VERIFYEXEC(verifyTodoCountJob);

        auto verifyTodoJob =
            Sink::Store::fetchOne<Todo>(Sink::Query().filter("uid", Sink::Query::Comparator(addedTodoUid)))
                .then([](const Todo &todo) { QCOMPARE(todo.getSummary(), {"Hello"}); });
        VERIFYEXEC(verifyTodoJob);
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

    void testModifyTodo()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto job = Sink::Store::fetchOne<Todo>(
            Sink::Query().filter("uid", Sink::Query::Comparator(addedTodoUid)))
                       .exec();
        job.waitForFinished();
        QVERIFY2(!job.errorCode(), "Fetching Todo failed");
        auto todo = job.value();

        auto incidence = KCalCore::ICalFormat().readIncidence(todo.getIcal());
        auto caltodo = incidence.dynamicCast<KCalCore::Todo>();
        QVERIFY2(caltodo, "Cannot convert to KCalCore todo");

        caltodo->setSummary("Hello World!");
        auto dummy = QSharedPointer<KCalCore::Todo>(caltodo);
        auto newical = KCalCore::ICalFormat().toICalString(dummy);

        todo.setIcal(newical.toUtf8());

        VERIFYEXEC(Sink::Store::modify(todo));

        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto verifyTodoCountJob = Sink::Store::fetchAll<Todo>({}).then(
            [](const QList<Todo::Ptr> &todos) { QCOMPARE(todos.size(), 2); });
        VERIFYEXEC(verifyTodoCountJob);

        auto verifyTodoJob =
            Sink::Store::fetchOne<Todo>(Sink::Query().filter("uid", Sink::Query::Comparator(addedTodoUid)))
                .then([](const Todo &todo) { QCOMPARE(todo.getSummary(), {"Hello World!"}); });
        VERIFYEXEC(verifyTodoJob);
    }

    void testSneakyModifyEvent()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        // Change the item without sink's knowledge
        {
            auto collection = ([this]() -> KDAV2::DavCollection {
                QUrl url(baseUrl);
                url.setUserName(username);
                url.setPassword(password);
                KDAV2::DavUrl davurl(url, KDAV2::CalDav);
                auto collectionsJob = new KDAV2::DavCollectionsFetchJob(davurl);
                collectionsJob->exec();
                Q_ASSERT(collectionsJob->error() == 0);
                return collectionsJob->collections()[0];
            })();

            auto itemList = ([&collection]() -> KDAV2::DavItem::List {
                auto cache = std::make_shared<KDAV2::EtagCache>();
                auto itemsListJob = new KDAV2::DavItemsListJob(collection.url(), cache);
                itemsListJob->exec();
                Q_ASSERT(itemsListJob->error() == 0);
                return itemsListJob->items();
            })();
            auto hollowDavItemIt =
                std::find_if(itemList.begin(), itemList.end(), [this](const KDAV2::DavItem &item) {
                    return item.url().url().path().endsWith(addedEventUid);
                });

            auto davitem = ([this, &collection, &hollowDavItemIt]() -> KDAV2::DavItem {
                QString itemUrl = collection.url().url().toEncoded() + addedEventUid;
                auto itemFetchJob = new KDAV2::DavItemFetchJob (*hollowDavItemIt);
                itemFetchJob->exec();
                Q_ASSERT(itemFetchJob->error() == 0);
                return itemFetchJob->item();
            })();

            auto incidence = KCalCore::ICalFormat().readIncidence(davitem.data());
            auto calevent = incidence.dynamicCast<KCalCore::Event>();
            QVERIFY2(calevent, "Cannot convert to KCalCore event");

            calevent->setSummary("Manual Hello World!");
            auto newical = KCalCore::ICalFormat().toICalString(calevent);

            davitem.setData(newical.toUtf8());
            auto itemModifyJob = new KDAV2::DavItemModifyJob(davitem);
            itemModifyJob->exec();
            QVERIFY2(itemModifyJob->error() == 0, "Cannot modify item");
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

    void testRemoveTodo()
    {
        VERIFYEXEC(Sink::Store::synchronize(Sink::Query().resourceFilter(mResourceInstanceIdentifier)));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));

        auto job = Sink::Store::fetchOne<Todo>(
            Sink::Query().filter("uid", Sink::Query::Comparator(addedTodoUid)))
                       .exec();
        job.waitForFinished();
        QVERIFY2(!job.errorCode(), "Fetching Todo failed");
        auto todo = job.value();

        VERIFYEXEC(Sink::Store::remove(todo));
        VERIFYEXEC(Sink::ResourceControl::flushReplayQueue(mResourceInstanceIdentifier));

        auto verifyTodoCountJob = Sink::Store::fetchAll<Todo>({}).then(
            [](const QList<Todo::Ptr> &todos) { QCOMPARE(todos.size(), 1); });
        VERIFYEXEC(verifyTodoCountJob);
    }
};

QTEST_MAIN(CalDavTest)

#include "caldavtest.moc"
