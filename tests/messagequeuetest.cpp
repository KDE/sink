#include <QtTest>

#include <QString>
#include <QQueue>

#include "clientapi.h"
#include "storage.h"
#include "messagequeue.h"

class MessageQueueTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        Akonadi2::Storage store(Akonadi2::Store::storageLocation(), "org.kde.dummy.testqueue", Akonadi2::Storage::ReadWrite);
        store.removeFromDisk();
    }

    void cleanupTestCase()
    {
    }

    void cleanup()
    {
        Akonadi2::Storage store(Akonadi2::Store::storageLocation(), "org.kde.dummy.testqueue", Akonadi2::Storage::ReadWrite);
        store.removeFromDisk();
    }

    void testEmpty()
    {
        MessageQueue queue(Akonadi2::Store::storageLocation(), "org.kde.dummy.testqueue");
        QVERIFY(queue.isEmpty());
        queue.enqueue("value");
        QVERIFY(!queue.isEmpty());
    }

    void testDequeueEmpty()
    {
        MessageQueue queue(Akonadi2::Store::storageLocation(), "org.kde.dummy.testqueue");
        bool gotValue = false;
        bool gotError = false;
        queue.dequeue([&](void *ptr, int size, std::function<void(bool success)> callback) {
            gotValue = true;
        },
        [&](const MessageQueue::Error &error) {
            gotError = true;
        });
        QVERIFY(!gotValue);
        QVERIFY(gotError);
    }

    void testDrained()
    {
        MessageQueue queue(Akonadi2::Store::storageLocation(), "org.kde.dummy.testqueue");
        QSignalSpy spy(&queue, SIGNAL(drained()));
        queue.enqueue("value1");

        queue.dequeue([](void *ptr, int size, std::function<void(bool success)> callback) {
            callback(true);
        }, [](const MessageQueue::Error &error) {});
        QCOMPARE(spy.size(), 1);
    }

    void testSyncDequeue()
    {
        QQueue<QByteArray> values;
        values << "value1";
        values << "value2";

        MessageQueue queue(Akonadi2::Store::storageLocation(), "org.kde.dummy.testqueue");
        for (const QByteArray &value : values) {
            queue.enqueue(value);
        }

        while (!queue.isEmpty()) {
            Log() << "start";
            const auto expected = values.dequeue();
            bool gotValue = false;
            bool gotError = false;
            queue.dequeue([&](void *ptr, int size, std::function<void(bool success)> callback) {
                if (QByteArray(static_cast<char*>(ptr), size) == expected) {
                    gotValue = true;
                }
                callback(true);
            },
            [&](const MessageQueue::Error &error) {
                gotError = true;
            });
            QVERIFY(gotValue);
            QVERIFY(!gotError);
        }
        QVERIFY(values.isEmpty());
    }

    void testAsyncDequeue()
    {
        QQueue<QByteArray> values;
        values << "value1";
        values << "value2";

        MessageQueue queue(Akonadi2::Store::storageLocation(), "org.kde.dummy.testqueue");
        for (const QByteArray &value : values) {
            queue.enqueue(value);
        }

        while (!queue.isEmpty()) {
            QEventLoop eventLoop;
            const auto expected = values.dequeue();
            bool gotValue = false;
            bool gotError = false;

            queue.dequeue([&](void *ptr, int size, std::function<void(bool success)> callback) {
                if (QByteArray(static_cast<char*>(ptr), size) == expected) {
                    gotValue = true;
                }
                auto timer = new QTimer();
                timer->setSingleShot(true);
                QObject::connect(timer, &QTimer::timeout, [timer, callback, &eventLoop]() {
                    delete timer;
                    callback(true);
                    eventLoop.exit();
                });
                timer->start(0);
            },
            [&](const MessageQueue::Error &error) {
                gotError = true;
            });
            eventLoop.exec();
            QVERIFY(gotValue);
            QVERIFY(!gotError);
        }
        QVERIFY(values.isEmpty());
    }

    /*
     * Dequeue's are async and we want to be able to enqueue new items in between.
     */
    void testNestedEnqueue()
    {
        MessageQueue queue(Akonadi2::Store::storageLocation(), "org.kde.dummy.testqueue");
        queue.enqueue("value1");

        bool gotError = false;
        queue.dequeue([&](void *ptr, int size, std::function<void(bool success)> callback) {
            queue.enqueue("value3");
            callback(true);
        },
        [&](const MessageQueue::Error &error) {
            gotError = true;
        });
        QVERIFY(!gotError);
    }


};

QTEST_MAIN(MessageQueueTest)
#include "messagequeuetest.moc"
