#include <QTest>
#include <QSignalSpy>
#include <QTimer>

#include <QString>
#include <QQueue>

#include "store.h"
#include "storage.h"
#include "messagequeue.h"
#include "log.h"
#include "test.h"
#include "testutils.h"

/**
 * Test of the messagequeue implementation.
 */
class MessageQueueTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        Sink::Test::initTest();
        Sink::Storage::DataStore store(Sink::Store::storageLocation(), "sink.dummy.testqueue", Sink::Storage::DataStore::ReadWrite);
        store.removeFromDisk();
    }

    void cleanupTestCase()
    {
    }

    void cleanup()
    {
        Sink::Storage::DataStore store(Sink::Store::storageLocation(), "sink.dummy.testqueue", Sink::Storage::DataStore::ReadWrite);
        store.removeFromDisk();
    }

    void testEmpty()
    {
        MessageQueue queue(Sink::Store::storageLocation(), "sink.dummy.testqueue");
        QVERIFY(queue.isEmpty());
        queue.enqueue("value");
        QVERIFY(!queue.isEmpty());
        queue.dequeue([](void *ptr, int size, std::function<void(bool success)> callback) { callback(true); }, [](const MessageQueue::Error &error) {});
        QVERIFY(queue.isEmpty());
    }

    void testDequeueEmpty()
    {
        MessageQueue queue(Sink::Store::storageLocation(), "sink.dummy.testqueue");
        bool gotValue = false;
        bool gotError = false;
        queue.dequeue([&](void *ptr, int size, std::function<void(bool success)> callback) { gotValue = true; }, [&](const MessageQueue::Error &error) { gotError = true; });
        QVERIFY(!gotValue);
        QVERIFY(!gotError);
    }

    void testEnqueue()
    {
        MessageQueue queue(Sink::Store::storageLocation(), "sink.dummy.testqueue");
        QSignalSpy spy(&queue, SIGNAL(messageReady()));
        queue.enqueue("value1");
        QCOMPARE(spy.size(), 1);
    }

    void testDrained()
    {
        MessageQueue queue(Sink::Store::storageLocation(), "sink.dummy.testqueue");
        QSignalSpy spy(&queue, SIGNAL(drained()));
        queue.enqueue("value1");

        queue.dequeue([](void *ptr, int size, std::function<void(bool success)> callback) { callback(true); }, [](const MessageQueue::Error &error) {});
        QCOMPARE(spy.size(), 1);
    }

    void testSyncDequeue()
    {
        QQueue<QByteArray> values;
        values << "value1";
        values << "value2";

        MessageQueue queue(Sink::Store::storageLocation(), "sink.dummy.testqueue");
        for (const QByteArray &value : values) {
            queue.enqueue(value);
        }

        while (!queue.isEmpty()) {
            SinkLog() << "start";
            const auto expected = values.dequeue();
            bool gotValue = false;
            bool gotError = false;
            queue.dequeue(
                [&](void *ptr, int size, std::function<void(bool success)> callback) {
                    if (QByteArray(static_cast<char *>(ptr), size) == expected) {
                        gotValue = true;
                    }
                    callback(true);
                },
                [&](const MessageQueue::Error &error) { gotError = true; });
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

        MessageQueue queue(Sink::Store::storageLocation(), "sink.dummy.testqueue");
        for (const QByteArray &value : values) {
            queue.enqueue(value);
        }

        while (!queue.isEmpty()) {
            QEventLoop eventLoop;
            const auto expected = values.dequeue();
            bool gotValue = false;
            bool gotError = false;

            queue.dequeue(
                [&](void *ptr, int size, std::function<void(bool success)> callback) {
                    if (QByteArray(static_cast<char *>(ptr), size) == expected) {
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
                [&](const MessageQueue::Error &error) { gotError = true; });
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
        MessageQueue queue(Sink::Store::storageLocation(), "sink.dummy.testqueue");
        queue.enqueue("value1");

        bool gotError = false;
        queue.dequeue(
            [&](void *ptr, int size, std::function<void(bool success)> callback) {
                queue.enqueue("value3");
                callback(true);
            },
            [&](const MessageQueue::Error &error) { gotError = true; });
        QVERIFY(!gotError);
    }

    void testBatchDequeue()
    {
        MessageQueue queue(Sink::Store::storageLocation(), "sink.dummy.testqueue");
        queue.enqueue("value1");
        queue.enqueue("value2");
        queue.enqueue("value3");

        int count = 0;
        queue.dequeueBatch(2, [&count](const QByteArray &data) {
                 count++;
                 ASYNCCOMPARE(data, QByteArray{"value"} + QByteArray::number(count));
                 return KAsync::null<void>();
             }).exec().waitForFinished();
        QCOMPARE(count, 2);

        queue.dequeueBatch(1, [&count](const QByteArray &data) {
                 count++;
                 ASYNCCOMPARE(data, QByteArray{"value"} + QByteArray::number(count));
                 return KAsync::null<void>();
             }).exec().waitForFinished();
        QCOMPARE(count, 3);
    }

    void testBatchDequeueDuringWriteTransaction()
    {
        MessageQueue queue(Sink::Store::storageLocation(), "sink.dummy.testqueue");
        queue.enqueue("value1");
        queue.enqueue("value2");
        queue.enqueue("value3");

        queue.startTransaction();
        //Inivisible to dequeues because in write transaction
        queue.enqueue("value4");

        int count = 0;
        queue.dequeueBatch(2, [&count](const QByteArray &data) {
                 count++;
                 ASYNCCOMPARE(data, QByteArray{"value"} + QByteArray::number(count));
                 return KAsync::null<void>();
             }).exec().waitForFinished();
        QCOMPARE(count, 2);

        queue.dequeueBatch(2, [&count](const QByteArray &data) {
                 count++;
                 ASYNCCOMPARE(data, QByteArray{"value"} + QByteArray::number(count));
                 return KAsync::null<void>();
             }).exec().waitForFinished();
        QCOMPARE(count, 3);
        QVERIFY(queue.isEmpty());

        //Commit value4
        queue.commit();
        QVERIFY(!queue.isEmpty());
        queue.dequeueBatch(2, [&count](const QByteArray &data) {
                 count++;
                 ASYNCCOMPARE(data, QByteArray{"value"} + QByteArray::number(count));
                 return KAsync::null<void>();
             }).exec().waitForFinished();
        QCOMPARE(count, 4);
    }

    void testBatchEnqueue()
    {
        MessageQueue queue(Sink::Store::storageLocation(), "sink.dummy.testqueue");
        QSignalSpy spy(&queue, SIGNAL(messageReady()));
        queue.startTransaction();
        queue.enqueue("value1");
        queue.enqueue("value2");
        queue.enqueue("value3");

        QVERIFY(queue.isEmpty());
        QCOMPARE(spy.count(), 0);

        queue.commit();

        QVERIFY(!queue.isEmpty());
        QCOMPARE(spy.count(), 1);
    }

    void testSortOrder()
    {
        MessageQueue queue(Sink::Store::storageLocation(), "sink.dummy.testqueue");
        queue.startTransaction();
        //Over 10 so we can make sure that 10 > 9
        const int num = 11;
        for (int i = 0; i < num; i++) {
            queue.enqueue("value" + QByteArray::number(i));
        }
        queue.commit();

        int count = 0;
        queue.dequeueBatch(num, [&count](const QByteArray &data) {
                 ASYNCCOMPARE(data, QByteArray{"value"} + QByteArray::number(count));
                 count++;
                 return KAsync::null<void>();
             }).exec().waitForFinished();
        QCOMPARE(count, num);

    }
};

QTEST_MAIN(MessageQueueTest)
#include "messagequeuetest.moc"
