#include <QtTest>

#include <QString>
#include <QSignalSpy>

#include "store.h"
#include "resourceconfig.h"
#include "resourcecontrol.h"
#include "modelresult.h"
#include "log.h"
#include "test.h"
#include "testutils.h"
#include "notifier.h"
#include "notification.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

/**
 * Test of complete system using the dummy resource.
 *
 * This test requires the dummy resource installed.
 */
class NotificationTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Sink::Test::initTest();
        ResourceConfig::addResource("sink.dummy.instance1", "sink.dummy");
        ResourceConfig::configureResource("sink.dummy.instance1", {{"populate", true}});
    }

    void cleanup()
    {
        VERIFYEXEC(Sink::Store::removeDataFromDisk("sink.dummy.instance1"));
    }

    void testSyncNotifications()
    {
        auto query = Query().resourceFilter("sink.dummy.instance1");
        query.setType<ApplicationDomain::Mail>();
        query.filter("id1");
        query.filter("id2");

        QList<Sink::Notification> statusNotifications;
        QList<Sink::Notification> infoNotifications;
        Sink::Notifier notifier("sink.dummy.instance1");
        notifier.registerHandler([&] (const Sink::Notification &n){
            SinkLogCtx(Sink::Log::Context{"dummyresourcetest"}) << "Received notification " << n;
            if (n.type == Notification::Status) {
                if (n.id == "changereplay") {
                    //We filter all changereplay notifications.
                    //Not the best way but otherwise the test becomes unstable and we currently
                    //only have the id to detect changereplay notifications.
                    return;
                }
                statusNotifications << n;
            }
            if (n.type == Notification::Info) {
                infoNotifications << n;
            }
        });

        // Ensure all local data is processed
        VERIFYEXEC(Sink::Store::synchronize(query));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        using namespace Sink::ApplicationDomain;
        {
            QList<Status> expected = {
                Status::ConnectedStatus,
                Status::BusyStatus,
                Status::ConnectedStatus,
            };
            qInfo() << "Received notifications " << statusNotifications;
            QVERIFY2(statusNotifications.size() <= expected.size(), "More notifications than expected.");
            QTRY_COMPARE(statusNotifications.size(), expected.size());
            qInfo() << "All received notifications " << statusNotifications;
            for (auto i = 0; i < statusNotifications.size(); i++) {
                QCOMPARE(statusNotifications.at(i).code, static_cast<int>(expected.at(i)));
            }
        }
        //Changereplay
        // It can happen that we get a changereplay notification pair first and then a second one at the end,
        // we therefore currently filter all changereplay notifications (see above).
        // QCOMPARE(statusNotifications.at(3).code, static_cast<int>(Sink::ApplicationDomain::Status::BusyStatus));
        // QCOMPARE(statusNotifications.at(4).code, static_cast<int>(Sink::ApplicationDomain::Status::ConnectedStatus));

        QTRY_COMPARE(infoNotifications.size(), 2);
        QCOMPARE(infoNotifications.at(0).code, static_cast<int>(ApplicationDomain::SyncStatus::SyncInProgress));
        QCOMPARE(infoNotifications.at(0).entities, QList<QByteArray>{} << "id1" << "id2");
        QCOMPARE(infoNotifications.at(1).code, static_cast<int>(ApplicationDomain::SyncStatus::SyncSuccess));
        QCOMPARE(infoNotifications.at(1).entities, QList<QByteArray>{} << "id1" << "id2");

        QCOMPARE(infoNotifications.at(1).code, static_cast<int>(ApplicationDomain::SyncStatus::SyncSuccess));
    }

    void testModelNotifications()
    {
        auto query = Query().resourceFilter("sink.dummy.instance1");
        query.setType<ApplicationDomain::Mail>();
        query.setFlags(Query::LiveQuery | Query::UpdateStatus);

        VERIFYEXEC(Sink::Store::synchronize(query));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        auto model = Sink::Store::loadModel<Sink::ApplicationDomain::Mail>(query);
        QTRY_VERIFY(model->data(QModelIndex(), Sink::Store::ChildrenFetchedRole).toBool());
        QVERIFY(model->rowCount() >= 1);

        QSignalSpy changedSpy(model.data(), &QAbstractItemModel::dataChanged);
        auto mail = model->index(0, 0, QModelIndex()).data(Sink::Store::DomainObjectRole).value<Mail::Ptr>();
        auto newQuery = query;
        newQuery.filter(mail->identifier());

        //We can make no assumptions about the amount of notifications because we collect on every dataChanged signal, even if the status did not change.
        QSet<int> status;
        QObject::connect(model.data(), &QAbstractItemModel::dataChanged, [&] (const QModelIndex &begin, const QModelIndex &end, const QVector<int> &roles) {
            QVERIFY(begin.row() == end.row());
            if (begin.row() == 0) {
                status << model->data(begin, Store::StatusRole).value<int>();
                // qWarning() << "New status: " << status.last() << roles;
            }
        });

        //This will trigger a modification of all previous items as well.
        VERIFYEXEC(Sink::Store::synchronize(newQuery));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance1"));

        QTRY_VERIFY(status.contains(static_cast<int>(ApplicationDomain::SyncStatus::SyncInProgress)) && static_cast<int>(ApplicationDomain::SyncStatus::SyncSuccess));
    }

    void testNotifier()
    {
        QList<int> status;
        Sink::Notifier notifier{Sink::Query{Sink::Query::LiveQuery}.resourceFilter("sink.dummy.instance2")};
        notifier.registerHandler([&] (const Sink::Notification &notification) {
            if (notification.type == Notification::Info) {
                status << notification.code;
            }
        });

        auto query = Query().resourceFilter("sink.dummy.instance2");
        query.setType<ApplicationDomain::Mail>();
        query.setFlags(Query::LiveQuery | Query::UpdateStatus);

        auto resource = ApplicationDomain::ApplicationDomainType::createEntity<ApplicationDomain::SinkResource>("", "sink.dummy.instance2");
        resource.setResourceType("sink.dummy");
        VERIFYEXEC(Store::create(resource));

        VERIFYEXEC(Sink::Store::synchronize(query));
        VERIFYEXEC(Sink::ResourceControl::flushMessageQueue(QByteArrayList() << "sink.dummy.instance2"));

        QTRY_COMPARE(status.size(), 2);
        //Sync progress of item
        QCOMPARE(status.at(0), static_cast<int>(ApplicationDomain::SyncStatus::SyncInProgress));
        QCOMPARE(status.at(1), static_cast<int>(ApplicationDomain::SyncStatus::SyncSuccess));
    }

};

QTEST_MAIN(NotificationTest)
#include "notificationtest.moc"
