#include <QByteArrayList>
#include <QDebug>
#include <storage.h>

int main(int argc, char *argv[])
{

    QByteArrayList arguments;
    for (int i = 0; i < argc; i++) {
        arguments << argv[i];
    }
    auto testDataPath = arguments.value(1);
    auto dbName = arguments.value(2);
    auto count = arguments.value(3).toInt();

    if (Sink::Storage::DataStore(testDataPath, dbName, Sink::Storage::DataStore::ReadOnly).exists()) {
        Sink::Storage::DataStore(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite).removeFromDisk();
    }

    qWarning() << "Creating db: " << testDataPath << dbName << count;
    Sink::Storage::DataStore store(testDataPath, dbName, Sink::Storage::DataStore::ReadWrite);
    auto transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
    for (int i = 0; i < count; i++) {
        if (!transaction) {
            qWarning() << "No valid transaction";
            return -1;
        }
        transaction.openDatabase("a", nullptr, false).write(QByteArray::number(i), "a");
        transaction.openDatabase("b", nullptr, false).write(QByteArray::number(i), "b");
        transaction.openDatabase("c", nullptr, false).write(QByteArray::number(i), "c");
        transaction.openDatabase("p", nullptr, false).write(QByteArray::number(i), "c");
        transaction.openDatabase("q", nullptr, false).write(QByteArray::number(i), "c");
        if (i > (count/2)) {
            for (int d = 0; d < 40; d++) {
                transaction.openDatabase("db" + QByteArray::number(d), nullptr, false).write(QByteArray::number(i), "a");
            }
        }
        if ((i % 1000) == 0) {
            transaction.commit();
            transaction = store.createTransaction(Sink::Storage::DataStore::ReadWrite);
        }
    }
    qWarning() << "Creating db done.";
    return 0;
}
