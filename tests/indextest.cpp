#include <QtTest>

#include <QString>
#include <QQueue>

#include "clientapi.h"
#include "storage.h"
#include "index.h"

class IndexTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        Akonadi2::Storage store(Akonadi2::Store::storageLocation(), "org.kde.dummy.testindex", Akonadi2::Storage::ReadWrite);
        store.removeFromDisk();
    }

    void cleanup()
    {
        Akonadi2::Storage store(Akonadi2::Store::storageLocation(), "org.kde.dummy.testindex", Akonadi2::Storage::ReadWrite);
        store.removeFromDisk();
    }

    void testIndex()
    {
        Index index(Akonadi2::Store::storageLocation(), "org.kde.dummy.testindex", Akonadi2::Storage::ReadWrite);
        index.add("key1", "value1");
        index.add("key1", "value2");
        index.add("key2", "value3");

        {
            QList<QByteArray> values;
            index.lookup(QByteArray("key1"), [&values](const QByteArray &value) {
                values << value;
            },
            [](const Index::Error &error){ qWarning() << "Error: "; });
            QCOMPARE(values.size(), 2);
        }
        {
            QList<QByteArray> values;
            index.lookup(QByteArray("key2"), [&values](const QByteArray &value) {
                values << value;
            },
            [](const Index::Error &error){ qWarning() << "Error: "; });
            QCOMPARE(values.size(), 1);
        }
        {
            QList<QByteArray> values;
            index.lookup(QByteArray("key3"), [&values](const QByteArray &value) {
                values << value;
            },
            [](const Index::Error &error){ qWarning() << "Error: "; });
            QCOMPARE(values.size(), 0);
        }
    }
};

QTEST_MAIN(IndexTest)
#include "indextest.moc"
