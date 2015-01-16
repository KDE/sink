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
        QByteArray value("value");
        queue.enqueue(value.data(), value.size());
        QVERIFY(!queue.isEmpty());
    }

    void testQueue()
    {
        QQueue<QByteArray> values;
        values << "value1";
        values << "value2";

        MessageQueue queue(Akonadi2::Store::storageLocation(), "org.kde.dummy.testqueue");
        for (const QByteArray &value : values) {
            queue.enqueue(value.data(), value.size());
        }

        while (!queue.isEmpty()) {
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

};

QTEST_MAIN(MessageQueueTest)
#include "messagequeuetest.moc"
