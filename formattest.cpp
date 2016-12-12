
namespace async {
    template<typename T>
    KAsync::Job<T> run(const std::function<T()> &f)
    {
        return KAsync::start<T>([f](KAsync::Future<T> &future) {
            auto result = QtConcurrent::run(f);
            auto watcher = new QFutureWatcher<T>;
            watcher->setFuture(result);
            QObject::connect(watcher, &QFutureWatcher<T>::finished, watcher, [&future, watcher]() {
                future.setValue(watcher->future().result());
                delete watcher;
                future.setFinished();
            });
        });
    }

}

void run()
{
    return KAsync::start<T>([f](KAsync::Future<T> &future) {
        auto result = QtConcurrent::run(f);
        auto watcher = new QFutureWatcher<T>;
        watcher->setFuture(result);
        QObject::connect(watcher, &QFutureWatcher<T>::finished, watcher, [&future]() {
            future.setFinished();
        });
    });
}

class ResourceAccessFactory {
public:
    static ResourceAccessFactory &instance()
    {
        static ResourceAccessFactory *instance = 0;
        if (!instance) {
            instance = new ResourceAccessFactory;
        }
        return *instance;
    }

    Sink::ResourceAccess::Ptr getAccess(const QByteArray &instanceIdentifier)
    {
        if (!mCache.contains(instanceIdentifier)) {
            //Reuse the pointer if something else kept the resourceaccess alive
            if (mWeakCache.contains(instanceIdentifier)) {
                auto sharedPointer = mWeakCache.value(instanceIdentifier).toStrongRef();
                if (sharedPointer) {
                    mCache.insert(instanceIdentifier, sharedPointer);
                }
            }
            if (!mCache.contains(instanceIdentifier)) {
                //Create a new instance if necessary
                auto sharedPointer = Sink::ResourceAccess::Ptr::create(instanceIdentifier);
                QObject::connect(sharedPointer.data(), &Sink::ResourceAccess::ready, sharedPointer.data(), [this, instanceIdentifier](bool ready) {
                    if (!ready) {
                        mCache.remove(instanceIdentifier);
                    }
                });
                mCache.insert(instanceIdentifier, sharedPointer);
                mWeakCache.insert(instanceIdentifier, sharedPointer);
            }
        }
        if (!mTimer.contains(instanceIdentifier)) {
            auto timer = new QTimer;
            //Drop connection after 3 seconds (which is a random value)
            QObject::connect(timer, &QTimer::timeout, timer, [this, instanceIdentifier]() {
                mCache.remove(instanceIdentifier);
            });
            timer->setInterval(3000);
            mTimer.insert(instanceIdentifier, timer);
        }
        auto timer = mTimer.value(instanceIdentifier);
        timer->start();
        return mCache.value(instanceIdentifier);
    }

    QHash<QByteArray, QWeakPointer<Sink::ResourceAccess> > mWeakCache;
    QHash<QByteArray, Sink::ResourceAccess::Ptr> mCache;
    QHash<QByteArray, QTimer*> mTimer;
};

class ChangeReplay : public QObject
{
    Q_OBJECT
public:

    typedef std::function<KAsync::Job<void>(const QByteArray &type, const QByteArray &key, const QByteArray &value)> ReplayFunction;

    ChangeReplay(const QString &resourceName, const ReplayFunction &replayFunction)
        : mStorage(storageLocation(), resourceName, Storage::ReadOnly),
        mChangeReplayStore(storageLocation(), resourceName + ".changereplay", Storage::ReadWrite),
        mReplayFunction(replayFunction)
    {

    }

    qint64 getLastReplayedRevision()
    {
        qint64 lastReplayedRevision = 0;
        auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadOnly);
        replayStoreTransaction.openDatabase().scan("lastReplayedRevision", [&lastReplayedRevision](const QByteArray &key, const QByteArray &value) -> bool {
            lastReplayedRevision = value.toLongLong();
            return false;
        }, [](const Storage::Error &) {
        });
        return lastReplayedRevision;
    }

    bool allChangesReplayed()
    {
        const qint64 topRevision = Storage::maxRevision(mStorage.createTransaction(Storage::ReadOnly));
        const qint64 lastReplayedRevision = getLastReplayedRevision();
        Trace() << "All changes replayed " << topRevision << lastReplayedRevision;
        return (lastReplayedRevision >= topRevision);
    }

signals:
    void changesReplayed();

public slots:
    void revisionChanged()
    {
        auto mainStoreTransaction = mStorage.createTransaction(Storage::ReadOnly);
        auto replayStoreTransaction = mChangeReplayStore.createTransaction(Storage::ReadWrite);
        qint64 lastReplayedRevision = 1;
        replayStoreTransaction.openDatabase().scan("lastReplayedRevision", [&lastReplayedRevision](const QByteArray &key, const QByteArray &value) -> bool {
            lastReplayedRevision = value.toLongLong();
            return false;
        }, [](const Storage::Error &) {
        });
        const qint64 topRevision = Storage::maxRevision(mainStoreTransaction);

        Trace() << "Changereplay from " << lastReplayedRevision << " to " << topRevision;
        if (lastReplayedRevision <= topRevision) {
            qint64 revision = lastReplayedRevision;
            for (;revision <= topRevision; revision++) {
                const auto uid = Storage::getUidFromRevision(mainStoreTransaction, revision);
                const auto type = Storage::getTypeFromRevision(mainStoreTransaction, revision);
                const auto key = Storage::assembleKey(uid, revision);
                mainStoreTransaction.openDatabase(type + ".main").scan(key, [&lastReplayedRevision, type, this](const QByteArray &key, const QByteArray &value) -> bool {
                    mReplayFunction(type, key, value).exec();
                    //TODO make for loop async, and pass to async replay function together with type
                    Trace() << "Replaying " << key;
                    return false;
                }, [key](const Storage::Error &) {
                    ErrorMsg() << "Failed to replay change " << key;
                });
            }
            revision--;
            replayStoreTransaction.openDatabase().write("lastReplayedRevision", QByteArray::number(revision));
            replayStoreTransaction.commit();
            Trace() << "Replayed until " << revision;
        }
        emit changesReplayed();
    }

private:
    Sink::Storage mStorage;
    Sink::Storage mChangeReplayStore;
    ReplayFunction mReplayFunction;
};

KAsync::Job<void> processPipeline()
{
    mPipeline->startTransaction();
    Trace() << "Cleaning up from " << mPipeline->cleanedUpRevision() + 1 << " to " << mLowerBoundRevision;
    for (qint64 revision = mPipeline->cleanedUpRevision() + 1; revision <= mLowerBoundRevision; revision++) {
        mPipeline->cleanupRevision(revision);
    }
    mPipeline->commit();

    //Go through all message queues
    auto it = QSharedPointer<QListIterator<MessageQueue*> >::create(mCommandQueues);
    return KAsync::doWhile(
        [it]() { return it->hasNext(); },
        [it, this](KAsync::Future<void> &future) {
            auto queue = it->next();
            processQueue(queue).then<void>([&future]() {
                Trace() << "Queue processed";
                future.setFinished();
            }).exec();
        }
    );
}

