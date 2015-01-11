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
            queue.dequeue([&](void *ptr, int size, std::function<void(bool success)> callback) {
                QCOMPARE(QByteArray(static_cast<char*>(ptr), size), expected);
                callback(true);
            }, [](const MessageQueue::Error &error) {

            });
        }
        Q_ASSERT(values.isEmpty());
    }

};

QTEST_MAIN(MessageQueueTest)
#include "messagequeuetest.moc"
