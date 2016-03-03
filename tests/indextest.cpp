#include <QtTest>

#include <QString>
#include <QQueue>

#include "store.h"
#include "storage.h"
#include "index.h"

/**
 * Test of the index implementation
 */
class IndexTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        Sink::Storage store("./testindex", "org.kde.dummy.testindex", Sink::Storage::ReadWrite);
        store.removeFromDisk();
    }

    void cleanup()
    {
        Sink::Storage store("./testindex", "org.kde.dummy.testindex", Sink::Storage::ReadWrite);
        store.removeFromDisk();
    }

    void testIndex()
    {
        Index index("./testindex", "org.kde.dummy.testindex", Sink::Storage::ReadWrite);
        // The first key is specifically a substring of the second key
        index.add("key", "value1");
        index.add("keyFoo", "value2");
        index.add("keyFoo", "value3");

        {
            QList<QByteArray> values;
            index.lookup(QByteArray("key"), [&values](const QByteArray &value) { values << value; }, [](const Index::Error &error) { qWarning() << "Error: "; });
            QCOMPARE(values.size(), 1);
        }
        {
            QList<QByteArray> values;
            index.lookup(QByteArray("keyFoo"), [&values](const QByteArray &value) { values << value; }, [](const Index::Error &error) { qWarning() << "Error: "; });
            QCOMPARE(values.size(), 2);
        }
        {
            QList<QByteArray> values;
            index.lookup(QByteArray("key3"), [&values](const QByteArray &value) { values << value; }, [](const Index::Error &error) { qWarning() << "Error: "; });
            QCOMPARE(values.size(), 0);
        }
        {
            QList<QByteArray> values;
            index.lookup(QByteArray("key"), [&values](const QByteArray &value) { values << value; }, [](const Index::Error &error) { qWarning() << "Error: "; }, true);
            QCOMPARE(values.size(), 3);
        }
    }
};

QTEST_MAIN(IndexTest)
#include "indextest.moc"
