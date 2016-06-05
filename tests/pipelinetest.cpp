#include <QtTest>

#include <QString>

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
#include "log.h"
#include "domainadaptor.h"
#include "definitions.h"
#include "adaptorfactoryregistry.h"

static void removeFromDisk(const QString &name)
{
    Sink::Storage store(Sink::Store::storageLocation(), name, Sink::Storage::ReadWrite);
    store.removeFromDisk();
}

static QList<QByteArray> getKeys(const QByteArray &dbEnv, const QByteArray &name)
{
    Sink::Storage store(Sink::storageLocation(), dbEnv, Sink::Storage::ReadOnly);
    auto transaction = store.createTransaction(Sink::Storage::ReadOnly);
    auto db = transaction.openDatabase(name, nullptr, false);
    QList<QByteArray> result;
    db.scan("", [&](const QByteArray &key, const QByteArray &value) {
        result << key;
        return true;
    });
    return result;
}

static QByteArray getEntity(const QByteArray &dbEnv, const QByteArray &name, const QByteArray &uid)
{
    Sink::Storage store(Sink::storageLocation(), dbEnv, Sink::Storage::ReadOnly);
    auto transaction = store.createTransaction(Sink::Storage::ReadOnly);
    auto db = transaction.openDatabase(name, nullptr, false);
    QByteArray result;
    db.scan(uid, [&](const QByteArray &key, const QByteArray &value) {
        result = value;
        return true;
    });
    return result;
}

flatbuffers::FlatBufferBuilder &createEvent(flatbuffers::FlatBufferBuilder &entityFbb, const QString &s = QString("summary"), const QString &d = QString())
{
    flatbuffers::FlatBufferBuilder eventFbb;
    eventFbb.Clear();
    {
        Sink::ApplicationDomain::Buffer::EventBuilder eventBuilder(eventFbb);
        auto eventLocation = eventBuilder.Finish();
        Sink::ApplicationDomain::Buffer::FinishEventBuffer(eventFbb, eventLocation);
    }

    flatbuffers::FlatBufferBuilder localFbb;
    {
        auto uid = localFbb.CreateString("testuid");
        auto summary = localFbb.CreateString(s.toStdString());
        auto description = localFbb.CreateString(d.toStdString());
        auto localBuilder = Sink::ApplicationDomain::Buffer::EventBuilder(localFbb);
        localBuilder.add_uid(uid);
        localBuilder.add_summary(summary);
        if (!d.isEmpty()) {
            localBuilder.add_description(description);
        }
        auto location = localBuilder.Finish();
        Sink::ApplicationDomain::Buffer::FinishEventBuffer(localFbb, location);
    }

    Sink::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, eventFbb.GetBufferPointer(), eventFbb.GetSize(), localFbb.GetBufferPointer(), localFbb.GetSize());
    return entityFbb;
}

QByteArray createEntityCommand(const flatbuffers::FlatBufferBuilder &entityFbb)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto type = fbb.CreateString(Sink::ApplicationDomain::getTypeName<Sink::ApplicationDomain::Event>().toStdString().data());
    auto delta = fbb.CreateVector<uint8_t>(entityFbb.GetBufferPointer(), entityFbb.GetSize());
    Sink::Commands::CreateEntityBuilder builder(fbb);
    builder.add_domainType(type);
    builder.add_delta(delta);
    auto location = builder.Finish();
    Sink::Commands::FinishCreateEntityBuffer(fbb, location);

    const QByteArray command(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command.data()), command.size());
        Q_ASSERT(Sink::Commands::VerifyCreateEntityBuffer(verifyer));
    }
    return command;
}

QByteArray modifyEntityCommand(const flatbuffers::FlatBufferBuilder &entityFbb, const QByteArray &uid, qint64 revision)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto type = fbb.CreateString(Sink::ApplicationDomain::getTypeName<Sink::ApplicationDomain::Event>().toStdString().data());
    auto id = fbb.CreateString(std::string(uid.constData(), uid.size()));
    auto summaryProperty = fbb.CreateString("summary");
    std::vector<flatbuffers::Offset<flatbuffers::String>> modified;
    modified.push_back(summaryProperty);
    auto delta = fbb.CreateVector<uint8_t>(entityFbb.GetBufferPointer(), entityFbb.GetSize());
    auto modifiedProperties = fbb.CreateVector(modified);
    // auto delta = Sink::EntityBuffer::appendAsVector(fbb, buffer.constData(), buffer.size());
    Sink::Commands::ModifyEntityBuilder builder(fbb);
    builder.add_domainType(type);
    builder.add_delta(delta);
    builder.add_revision(revision);
    builder.add_entityId(id);
    builder.add_modifiedProperties(modifiedProperties);
    auto location = builder.Finish();
    Sink::Commands::FinishModifyEntityBuffer(fbb, location);

    const QByteArray command(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command.data()), command.size());
        Q_ASSERT(Sink::Commands::VerifyCreateEntityBuffer(verifyer));
    }
    return command;
}

QByteArray deleteEntityCommand(const QByteArray &uid, qint64 revision)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto type = fbb.CreateString(Sink::ApplicationDomain::getTypeName<Sink::ApplicationDomain::Event>().toStdString().data());
    auto id = fbb.CreateString(std::string(uid.constData(), uid.size()));
    Sink::Commands::DeleteEntityBuilder builder(fbb);
    builder.add_domainType(type);
    builder.add_revision(revision);
    builder.add_entityId(id);
    auto location = builder.Finish();
    Sink::Commands::FinishDeleteEntityBuffer(fbb, location);

    const QByteArray command(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command.data()), command.size());
        Q_ASSERT(Sink::Commands::VerifyDeleteEntityBuffer(verifyer));
    }
    return command;
}

class TestProcessor : public Sink::Preprocessor
{
public:
    void newEntity(const QByteArray &uid, qint64 revision, Sink::ApplicationDomain::BufferAdaptor &newEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        newUids << uid;
        newRevisions << revision;
    }

    void modifiedEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::ApplicationDomain::BufferAdaptor &newEntity,
        Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        modifiedUids << uid;
        modifiedRevisions << revision;
    }

    void deletedEntity(const QByteArray &uid, qint64 revision, const Sink::ApplicationDomain::BufferAdaptor &oldEntity, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        deletedUids << uid;
        deletedRevisions << revision;
        deletedSummaries << oldEntity.getProperty("summary").toByteArray();
    }

    QList<QByteArray> newUids;
    QList<qint64> newRevisions;
    QList<QByteArray> modifiedUids;
    QList<qint64> modifiedRevisions;
    QList<QByteArray> deletedUids;
    QList<qint64> deletedRevisions;
    QList<QByteArray> deletedSummaries;
};

/**
 * Test of the pipeline implementation to ensure new revisions are created correctly in the database.
 */
class PipelineTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        Sink::Log::setDebugOutputLevel(Sink::Log::Trace);
        Sink::AdaptorFactoryRegistry::instance().registerFactory<Sink::ApplicationDomain::Event, TestEventAdaptorFactory>("test");
    }

    void init()
    {
        removeFromDisk("org.kde.pipelinetest.instance1");
    }

    void testCreate()
    {
        flatbuffers::FlatBufferBuilder entityFbb;
        auto command = createEntityCommand(createEvent(entityFbb));

        Sink::Pipeline pipeline("org.kde.pipelinetest.instance1");
        pipeline.setResourceType("test");

        pipeline.startTransaction();
        pipeline.newEntity(command.constData(), command.size());
        pipeline.commit();

        auto result = getKeys("org.kde.pipelinetest.instance1", "event.main");
        qDebug() << result;
        QCOMPARE(result.size(), 1);
    }

    void testModify()
    {
        flatbuffers::FlatBufferBuilder entityFbb;
        auto command = createEntityCommand(createEvent(entityFbb, "summary", "description"));

        Sink::Pipeline pipeline("org.kde.pipelinetest.instance1");
        pipeline.setResourceType("test");

        auto adaptorFactory = QSharedPointer<TestEventAdaptorFactory>::create();

        // Create the initial revision
        pipeline.startTransaction();
        pipeline.newEntity(command.constData(), command.size());
        pipeline.commit();

        // Get uid of written entity
        auto keys = getKeys("org.kde.pipelinetest.instance1", "event.main");
        QCOMPARE(keys.size(), 1);
        const auto key = keys.first();
        const auto uid = Sink::Storage::uidFromKey(key);

        // Execute the modification
        entityFbb.Clear();
        auto modifyCommand = modifyEntityCommand(createEvent(entityFbb, "summary2"), uid, 1);
        pipeline.startTransaction();
        pipeline.modifiedEntity(modifyCommand.constData(), modifyCommand.size());
        pipeline.commit();

        // Ensure we've got the new revision with the modification
        auto buffer = getEntity("org.kde.pipelinetest.instance1", "event.main", Sink::Storage::assembleKey(uid, 2));
        QVERIFY(!buffer.isEmpty());
        Sink::EntityBuffer entityBuffer(buffer.data(), buffer.size());
        auto adaptor = adaptorFactory->createAdaptor(entityBuffer.entity());
        QVERIFY2(adaptor->getProperty("summary").toString() == QString("summary2"), "The modification isn't applied.");
        // Ensure we didn't modify anything else
        QVERIFY2(adaptor->getProperty("description").toString() == QString("description"), "The modification has sideeffects.");

        // Both revisions are in the store at this point
        QCOMPARE(getKeys("org.kde.pipelinetest.instance1", "event.main").size(), 2);

        // Cleanup old revisions
        pipeline.startTransaction();
        pipeline.cleanupRevision(2);
        pipeline.commit();

        // And now only the latest revision is left
        QCOMPARE(getKeys("org.kde.pipelinetest.instance1", "event.main").size(), 1);
    }

    void testModifyWithUnrelatedOperationInbetween()
    {
        flatbuffers::FlatBufferBuilder entityFbb;
        auto command = createEntityCommand(createEvent(entityFbb));

        Sink::Pipeline pipeline("org.kde.pipelinetest.instance1");
        pipeline.setResourceType("test");

        auto adaptorFactory = QSharedPointer<TestEventAdaptorFactory>::create();

        // Create the initial revision
        pipeline.startTransaction();
        pipeline.newEntity(command.constData(), command.size());
        pipeline.commit();

        // Get uid of written entity
        auto keys = getKeys("org.kde.pipelinetest.instance1", "event.main");
        QCOMPARE(keys.size(), 1);
        const auto uid = Sink::Storage::uidFromKey(keys.first());


        // Create another operation inbetween
        {
            entityFbb.Clear();
            auto command = createEntityCommand(createEvent(entityFbb));
            pipeline.startTransaction();
            pipeline.newEntity(command.constData(), command.size());
            pipeline.commit();
        }

        // Execute the modification on revision 2
        entityFbb.Clear();
        auto modifyCommand = modifyEntityCommand(createEvent(entityFbb, "summary2"), uid, 2);
        pipeline.startTransaction();
        pipeline.modifiedEntity(modifyCommand.constData(), modifyCommand.size());
        pipeline.commit();

        // Ensure we've got the new revision with the modification
        auto buffer = getEntity("org.kde.pipelinetest.instance1", "event.main", Sink::Storage::assembleKey(uid, 3));
        QVERIFY(!buffer.isEmpty());
        Sink::EntityBuffer entityBuffer(buffer.data(), buffer.size());
        auto adaptor = adaptorFactory->createAdaptor(entityBuffer.entity());
        QCOMPARE(adaptor->getProperty("summary").toString(), QString("summary2"));
    }

    void testDelete()
    {
        flatbuffers::FlatBufferBuilder entityFbb;
        auto command = createEntityCommand(createEvent(entityFbb));
        Sink::Pipeline pipeline("org.kde.pipelinetest.instance1");
        pipeline.setResourceType("test");

        // Create the initial revision
        pipeline.startTransaction();
        pipeline.newEntity(command.constData(), command.size());
        pipeline.commit();

        auto result = getKeys("org.kde.pipelinetest.instance1", "event.main");
        QCOMPARE(result.size(), 1);

        const auto uid = Sink::Storage::uidFromKey(result.first());

        // Delete entity
        auto deleteCommand = deleteEntityCommand(uid, 1);
        pipeline.startTransaction();
        pipeline.deletedEntity(deleteCommand.constData(), deleteCommand.size());
        pipeline.commit();

        // We have a new revision that indicates the deletion
        QCOMPARE(getKeys("org.kde.pipelinetest.instance1", "event.main").size(), 2);

        // Cleanup old revisions
        pipeline.startTransaction();
        pipeline.cleanupRevision(2);
        pipeline.commit();

        // And all revisions are gone
        QCOMPARE(getKeys("org.kde.pipelinetest.instance1", "event.main").size(), 0);
    }

    void testPreprocessor()
    {
        flatbuffers::FlatBufferBuilder entityFbb;

        TestProcessor testProcessor;

        Sink::Pipeline pipeline("org.kde.pipelinetest.instance1");
        pipeline.setResourceType("test");
        pipeline.setPreprocessors("event", QVector<Sink::Preprocessor *>() << &testProcessor);
        pipeline.startTransaction();
        // pipeline.setAdaptorFactory("event", QSharedPointer<TestEventAdaptorFactory>::create());

        // Actual test
        {
            auto command = createEntityCommand(createEvent(entityFbb));
            pipeline.newEntity(command.constData(), command.size());
            QCOMPARE(testProcessor.newUids.size(), 1);
            QCOMPARE(testProcessor.newRevisions.size(), 1);
            // Key doesn't contain revision and is just the uid
            QCOMPARE(testProcessor.newUids.at(0), Sink::Storage::uidFromKey(testProcessor.newUids.at(0)));
        }
        pipeline.commit();
        entityFbb.Clear();
        pipeline.startTransaction();
        auto keys = getKeys("org.kde.pipelinetest.instance1", "event.main");
        QCOMPARE(keys.size(), 1);
        const auto uid = Sink::Storage::uidFromKey(keys.first());
        {
            auto modifyCommand = modifyEntityCommand(createEvent(entityFbb, "summary2"), uid, 1);
            pipeline.modifiedEntity(modifyCommand.constData(), modifyCommand.size());
            QCOMPARE(testProcessor.modifiedUids.size(), 1);
            QCOMPARE(testProcessor.modifiedRevisions.size(), 1);
            // Key doesn't contain revision and is just the uid
            QCOMPARE(testProcessor.modifiedUids.at(0), Sink::Storage::uidFromKey(testProcessor.modifiedUids.at(0)));
        }
        pipeline.commit();
        entityFbb.Clear();
        pipeline.startTransaction();
        {
            auto deleteCommand = deleteEntityCommand(uid, 1);
            pipeline.deletedEntity(deleteCommand.constData(), deleteCommand.size());
            QCOMPARE(testProcessor.deletedUids.size(), 1);
            QCOMPARE(testProcessor.deletedUids.size(), 1);
            QCOMPARE(testProcessor.deletedSummaries.size(), 1);
            // Key doesn't contain revision and is just the uid
            QCOMPARE(testProcessor.deletedUids.at(0), Sink::Storage::uidFromKey(testProcessor.deletedUids.at(0)));
            QCOMPARE(testProcessor.deletedSummaries.at(0), QByteArray("summary2"));
        }
    }
};

QTEST_MAIN(PipelineTest)
#include "pipelinetest.moc"
