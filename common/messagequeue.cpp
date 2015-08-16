#include "messagequeue.h"
#include "storage.h"
#include <QDebug>
#include <log.h>

static KAsync::Job<void> waitForCompletion(QList<KAsync::Future<void> > &futures)
{
    auto context = new QObject;
    return KAsync::start<void>([futures, context](KAsync::Future<void> &future) {
        const auto total = futures.size();
        auto count = QSharedPointer<int>::create();
        int i = 0;
        for (KAsync::Future<void> subFuture : futures) {
            i++;
            if (subFuture.isFinished()) {
                *count += 1;
                continue;
            }
            //FIXME bind lifetime all watcher to future (repectively the main job
            auto watcher = QSharedPointer<KAsync::FutureWatcher<void> >::create();
            QObject::connect(watcher.data(), &KAsync::FutureWatcher<void>::futureReady,
            [count, total, &future](){
                *count += 1;
                if (*count == total) {
                    future.setFinished();
                }
            });
            watcher->setFuture(subFuture);
            context->setProperty(QString("future%1").arg(i).toLatin1().data(), QVariant::fromValue(watcher));
        }
        if (*count == total) {
            future.setFinished();
        }
    }).then<void>([context]() {
        delete context;
    });
}

MessageQueue::MessageQueue(const QString &storageRoot, const QString &name)
    : mStorage(storageRoot, name, Akonadi2::Storage::ReadWrite)
{

}

void MessageQueue::enqueue(void const *msg, size_t size)
{
    enqueue(QByteArray::fromRawData(static_cast<const char*>(msg), size));
}

void MessageQueue::enqueue(const QByteArray &value)
{
    auto transaction = std::move(mStorage.createTransaction(Akonadi2::Storage::ReadWrite));
    const qint64 revision = Akonadi2::Storage::maxRevision(transaction) + 1;
    const QByteArray key = QString("%1").arg(revision).toUtf8();
    transaction.write(key, value);
    Akonadi2::Storage::setMaxRevision(transaction, revision);
    transaction.commit();
    emit messageReady();
}

void MessageQueue::dequeue(const std::function<void(void *ptr, int size, std::function<void(bool success)>)> &resultHandler,
                           const std::function<void(const Error &error)> &errorHandler)
{
    bool readValue = false;
    mStorage.createTransaction(Akonadi2::Storage::ReadOnly).scan("", [this, resultHandler, errorHandler, &readValue](const QByteArray &key, const QByteArray &value) -> bool {
        if (Akonadi2::Storage::isInternalKey(key)) {
            return true;
        }
        readValue = true;
        //We need a copy of the key here, otherwise we can't store it in the lambda (the pointers will become invalid)
        const auto keyCopy  = QByteArray(key.constData(), key.size());
        resultHandler(const_cast<void*>(static_cast<const void*>(value.data())), value.size(), [this, keyCopy, errorHandler](bool success) {
            if (success) {
                mStorage.createTransaction(Akonadi2::Storage::ReadWrite).remove(keyCopy, [errorHandler, keyCopy](const Akonadi2::Storage::Error &error) {
                    ErrorMsg() << "Error while removing value" << error.message << keyCopy;
                    //Don't call the errorhandler in here, we already called the result handler
                });
                if (isEmpty()) {
                    emit this->drained();
                }
            } else {
                //TODO re-enqueue?
            }
        });
        return false;
    },
    [errorHandler](const Akonadi2::Storage::Error &error) {
        ErrorMsg() << "Error while retrieving value" << error.message;
        errorHandler(Error(error.store, error.code, error.message));
    }
    );
    if (!readValue) {
        errorHandler(Error("messagequeue", -1, "No message found"));
    }
}

KAsync::Job<void> MessageQueue::dequeueBatch(int maxBatchSize, const std::function<KAsync::Job<void>(const QByteArray &)> &resultHandler)
{
    auto resultCount = QSharedPointer<int>::create(0);
    auto keyList = QSharedPointer<QByteArrayList>::create();
    return KAsync::start<void>([this, maxBatchSize, resultHandler, resultCount, keyList](KAsync::Future<void> &future) {
        int count = 0;
        QList<KAsync::Future<void> > waitCondition;
        mStorage.createTransaction(Akonadi2::Storage::ReadOnly).scan("", [this, resultHandler, resultCount, keyList, &count, maxBatchSize, &waitCondition](const QByteArray &key, const QByteArray &value) -> bool {
            if (Akonadi2::Storage::isInternalKey(key)) {
                return true;
            }
            //We need a copy of the key here, otherwise we can't store it in the lambda (the pointers will become invalid)
            keyList->append(QByteArray(key.constData(), key.size()));

            waitCondition << resultHandler(value).exec();

            count++;
            if (count <= maxBatchSize) {
                return true;
            }
            return false;
        },
        [](const Akonadi2::Storage::Error &error) {
            ErrorMsg() << "Error while retrieving value" << error.message;
            // errorHandler(Error(error.store, error.code, error.message));
        });

        ::waitForCompletion(waitCondition).then<void>([this, keyList, &future]() {
            Trace() << "Dequeue complete, removing values " << *keyList;
            auto transaction = std::move(mStorage.createTransaction(Akonadi2::Storage::ReadWrite));
            for (const auto &key : *keyList) {
                transaction.remove(key, [key](const Akonadi2::Storage::Error &error) {
                    ErrorMsg() << "Error while removing value" << error.message << key;
                    //Don't call the errorhandler in here, we already called the result handler
                });
            }
            transaction.commit();
            if (keyList->isEmpty()) {
                future.setError(-1, "No message found");
                future.setFinished();
            } else {
                if (isEmpty()) {
                    emit this->drained();
                }
                future.setFinished();
            }
        }).exec();
    });
}

bool MessageQueue::isEmpty()
{
    int count = 0;
    mStorage.createTransaction(Akonadi2::Storage::ReadOnly).scan("", [&count](const QByteArray &key, const QByteArray &value) -> bool {
        if (!Akonadi2::Storage::isInternalKey(key)) {
            count++;
            return false;
        }
        return true;
    },
    [](const Akonadi2::Storage::Error &error) {
        qDebug() << "Error while checking if empty" << error.message;
    });
    return count == 0;
}

