#include <QtTest>

#include <QString>
#include <QSharedPointer>

#include "testimplementations.h"

#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"
#include "modifyentity_generated.h"
#include "deleteentity_generated.h"
#include "dummyresource/resourcefactory.h"
#include "store.h"
#include "commands.h"
#include "entitybuffer.h"
#include "resourceconfig.h"
#include "pipeline.h"
#include "synchronizer.h"
#include "commandprocessor.h"
#include "log.h"
#include "domainadaptor.h"
#include "definitions.h"
#include "adaptorfactoryregistry.h"
#include "storage/key.h"
#include "genericresource.h"
#include "testutils.h"
#include "test.h"

class TestSynchronizer: public Sink::Synchronizer {
public:
    TestSynchronizer(const Sink::ResourceContext &context): Sink::Synchronizer(context)
    {

    }

    QMap<QByteArray, std::function<void()>> mSyncCallbacks;

    KAsync::Job<void> synchronizeWithSource(const Sink::QueryBase &query) override
    {
        return KAsync::start([this, query] {
            Q_ASSERT(mSyncCallbacks.contains(query.id()));
            mSyncCallbacks.value(query.id())();
        });
    }

    void createOrModify(const QByteArray &rid, Sink::ApplicationDomain::ApplicationDomainType &entity)
    {
        Sink::Synchronizer::createOrModify("calendar", rid, entity);
    }

    void scanForRemovals(const QSet<QByteArray> &set)
    {
        Sink::Synchronizer::scanForRemovals("calendar", [&](const QByteArray &remoteId) {
            return set.contains(remoteId);
        });
    }

    QByteArray resolveRemoteId(const QByteArray &remoteId) {
        return syncStore().resolveRemoteId("calendar", remoteId);
    }

    void synchronize(std::function<void()> callback, const QByteArray &id = {}, Synchronizer::SyncRequest::RequestOptions options = Synchronizer::SyncRequest::NoOptions) {
        mSyncCallbacks.insert(id, callback);
        Sink::Query query;
        query.setId(id);
        addToQueue(Synchronizer::SyncRequest{query, id, options});
        VERIFYEXEC(processSyncQueue());
    }
};

class SynchronizerTest : public QObject
{
    Q_OBJECT

    QByteArray instanceIdentifier()
    {
        return "synchronizertest.instance1";
    }

    Sink::ResourceContext getContext()
    {
        return Sink::ResourceContext{instanceIdentifier(), "test", Sink::AdaptorFactoryRegistry::instance().getFactories("test")};
    }

private slots:
    void initTestCase()
    {
        Sink::Test::initTest();
        Sink::Storage::DataStore{Sink::Store::storageLocation(), instanceIdentifier(), Sink::Storage::DataStore::ReadWrite}.removeFromDisk();
        Sink::AdaptorFactoryRegistry::instance().registerFactory<Sink::ApplicationDomain::Calendar, DomainTypeAdaptorFactory<Sink::ApplicationDomain::Calendar>>("test");
    }

    void init()
    {
        Sink::GenericResource::removeFromDisk(instanceIdentifier());
    }

    void testSynchronizer()
    {
        const auto context = getContext();
        Sink::Pipeline pipeline(context, instanceIdentifier());
        Sink::CommandProcessor processor(&pipeline, instanceIdentifier(), Sink::Log::Context{"processor"});

        auto synchronizer = QSharedPointer<TestSynchronizer>::create(context);
        processor.setSynchronizer(synchronizer);

        synchronizer->setSecret("secret");

        synchronizer->synchronize([&] {
            Sink::ApplicationDomain::Calendar calendar;
            calendar.setName("Name");
            synchronizer->createOrModify("1", calendar);
        });

        VERIFYEXEC(processor.processAllMessages());

        const auto sinkId = synchronizer->resolveRemoteId("1");
        QVERIFY(!sinkId.isEmpty());

        {
            Sink::Storage::EntityStore store(context, {"entitystore"});
            QVERIFY(store.contains("calendar", sinkId));
            QVERIFY(store.exists("calendar", sinkId));
        }

        //Remove the calendar
        synchronizer->synchronize([&] {
            synchronizer->scanForRemovals({});
        });
        synchronizer->revisionChanged();
        VERIFYEXEC(processor.processAllMessages());

        {
            Sink::Storage::EntityStore store(context, {"entitystore"});
            QVERIFY(!store.exists("calendar", sinkId));
            QVERIFY(store.contains("calendar", sinkId));
        }

        //Recreate the same calendar
        synchronizer->synchronize([&] {
            Sink::ApplicationDomain::Calendar calendar;
            calendar.setName("Name");
            synchronizer->createOrModify("1", calendar);
        });
        synchronizer->revisionChanged();
        VERIFYEXEC(processor.processAllMessages());
        {
            Sink::Storage::EntityStore store(context, {"entitystore"});
            QVERIFY(store.contains("calendar", sinkId));
            QVERIFY(store.exists("calendar", sinkId));
        }
    }

    /*
     * Ensure the flushed content is available during the next sync request
     */
    void testFlush()
    {
        const auto context = getContext();
        Sink::Pipeline pipeline(context, instanceIdentifier());
        Sink::CommandProcessor processor(&pipeline, instanceIdentifier(), Sink::Log::Context{"processor"});

        auto synchronizer = QSharedPointer<TestSynchronizer>::create(context);
        processor.setSynchronizer(synchronizer);

        synchronizer->setSecret("secret");

        QByteArray sinkId;
        synchronizer->synchronize([&] {
            Sink::ApplicationDomain::Calendar calendar;
            calendar.setName("Name");
            synchronizer->createOrModify("1", calendar);
            sinkId = synchronizer->resolveRemoteId("1");
        }, "1");
        QVERIFY(!sinkId.isEmpty());

        //With a flush the calendar should be available during the next sync
        synchronizer->synchronize([&] {
            Sink::Storage::EntityStore store(context, {"entitystore"});
            QVERIFY(store.contains("calendar", sinkId));

        }, "2", Sink::Synchronizer::SyncRequest::RequestFlush);

        VERIFYEXEC(processor.processAllMessages());
    }

};

QTEST_MAIN(SynchronizerTest)
#include "synchronizertest.moc"
