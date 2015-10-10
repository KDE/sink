#include <QtTest>

#include <QString>

#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"
#include "modifyentity_generated.h"
#include "deleteentity_generated.h"
#include "dummyresource/resourcefactory.h"
#include "clientapi.h"
#include "synclistresult.h"
#include "commands.h"
#include "entitybuffer.h"
#include "resourceconfig.h"
#include "pipeline.h"
#include "log.h"
#include "domainadaptor.h"

class TestEventAdaptorFactory : public DomainTypeAdaptorFactory<Akonadi2::ApplicationDomain::Event, Akonadi2::ApplicationDomain::Buffer::Event, Akonadi2::ApplicationDomain::Buffer::EventBuilder>
{
public:
    TestEventAdaptorFactory()
        : DomainTypeAdaptorFactory()
    {
    }

    virtual ~TestEventAdaptorFactory() {};
};

static void removeFromDisk(const QString &name)
{
    Akonadi2::Storage store(Akonadi2::Store::storageLocation(), name, Akonadi2::Storage::ReadWrite);
    store.removeFromDisk();
}

static QList<QByteArray> getKeys(const QByteArray &dbEnv, const QByteArray &name)
{
    Akonadi2::Storage store(Akonadi2::storageLocation(), dbEnv, Akonadi2::Storage::ReadOnly);
    auto transaction = store.createTransaction(Akonadi2::Storage::ReadOnly);
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
    Akonadi2::Storage store(Akonadi2::storageLocation(), dbEnv, Akonadi2::Storage::ReadOnly);
    auto transaction = store.createTransaction(Akonadi2::Storage::ReadOnly);
    auto db = transaction.openDatabase(name, nullptr, false);
    QByteArray result;
    db.scan(uid, [&](const QByteArray &key, const QByteArray &value) {
        result = value;
        return true;
    });
    return result;
}

flatbuffers::FlatBufferBuilder &createEvent(flatbuffers::FlatBufferBuilder &entityFbb, const QString &s = QString("summary"))
{
    flatbuffers::FlatBufferBuilder eventFbb;
    eventFbb.Clear();
    {
        Akonadi2::ApplicationDomain::Buffer::EventBuilder eventBuilder(eventFbb);
        auto eventLocation = eventBuilder.Finish();
        Akonadi2::ApplicationDomain::Buffer::FinishEventBuffer(eventFbb, eventLocation);
    }

    flatbuffers::FlatBufferBuilder localFbb;
    {
        auto uid = localFbb.CreateString("testuid");
        auto summary = localFbb.CreateString(s.toStdString());
        auto localBuilder = Akonadi2::ApplicationDomain::Buffer::EventBuilder(localFbb);
        localBuilder.add_uid(uid);
        localBuilder.add_summary(summary);
        auto location = localBuilder.Finish();
        Akonadi2::ApplicationDomain::Buffer::FinishEventBuffer(localFbb, location);
    }

    Akonadi2::EntityBuffer::assembleEntityBuffer(entityFbb, 0, 0, eventFbb.GetBufferPointer(), eventFbb.GetSize(), localFbb.GetBufferPointer(), localFbb.GetSize());
    return entityFbb;
}

QByteArray createEntityCommand(const flatbuffers::FlatBufferBuilder &entityFbb)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto type = fbb.CreateString(Akonadi2::ApplicationDomain::getTypeName<Akonadi2::ApplicationDomain::Event>().toStdString().data());
    auto delta = fbb.CreateVector<uint8_t>(entityFbb.GetBufferPointer(), entityFbb.GetSize());
    Akonadi2::Commands::CreateEntityBuilder builder(fbb);
    builder.add_domainType(type);
    builder.add_delta(delta);
    auto location = builder.Finish();
    Akonadi2::Commands::FinishCreateEntityBuffer(fbb, location);

    const QByteArray command(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command.data()), command.size());
        Q_ASSERT(Akonadi2::Commands::VerifyCreateEntityBuffer(verifyer));
    }
    return command;
}

QByteArray modifyEntityCommand(const flatbuffers::FlatBufferBuilder &entityFbb, const QByteArray &uid, qint64 revision)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto type = fbb.CreateString(Akonadi2::ApplicationDomain::getTypeName<Akonadi2::ApplicationDomain::Event>().toStdString().data());
    auto id = fbb.CreateString(std::string(uid.constData(), uid.size()));
    auto delta = fbb.CreateVector<uint8_t>(entityFbb.GetBufferPointer(), entityFbb.GetSize());
    // auto delta = Akonadi2::EntityBuffer::appendAsVector(fbb, buffer.constData(), buffer.size());
    Akonadi2::Commands::ModifyEntityBuilder builder(fbb);
    builder.add_domainType(type);
    builder.add_delta(delta);
    builder.add_revision(revision);
    builder.add_entityId(id);
    auto location = builder.Finish();
    Akonadi2::Commands::FinishModifyEntityBuffer(fbb, location);

    const QByteArray command(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command.data()), command.size());
        Q_ASSERT(Akonadi2::Commands::VerifyCreateEntityBuffer(verifyer));
    }
    return command;
}

QByteArray deleteEntityCommand(const QByteArray &uid, qint64 revision)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto type = fbb.CreateString(Akonadi2::ApplicationDomain::getTypeName<Akonadi2::ApplicationDomain::Event>().toStdString().data());
    auto id = fbb.CreateString(std::string(uid.constData(), uid.size()));
    Akonadi2::Commands::DeleteEntityBuilder builder(fbb);
    builder.add_domainType(type);
    builder.add_revision(revision);
    builder.add_entityId(id);
    auto location = builder.Finish();
    Akonadi2::Commands::FinishDeleteEntityBuffer(fbb, location);

    const QByteArray command(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
    {
        flatbuffers::Verifier verifyer(reinterpret_cast<const uint8_t *>(command.data()), command.size());
        Q_ASSERT(Akonadi2::Commands::VerifyDeleteEntityBuffer(verifyer));
    }
    return command;
}

class PipelineTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        Akonadi2::Log::setDebugOutputLevel(Akonadi2::Log::Trace);
    }

    void init()
    {
        removeFromDisk("org.kde.pipelinetest.instance1");
    }

    void testCreate()
    {
        flatbuffers::FlatBufferBuilder entityFbb;
        auto command = createEntityCommand(createEvent(entityFbb));

        Akonadi2::Pipeline pipeline("org.kde.pipelinetest.instance1");
        pipeline.startTransaction();
        pipeline.newEntity(command.constData(), command.size());
        pipeline.commit();

        auto result = getKeys("org.kde.pipelinetest.instance1", "event.main");
        QCOMPARE(result.size(), 1);
    }

    void testModify()
    {
        flatbuffers::FlatBufferBuilder entityFbb;
        auto command = createEntityCommand(createEvent(entityFbb));

        Akonadi2::Pipeline pipeline("org.kde.pipelinetest.instance1");

        auto adaptorFactory = QSharedPointer<TestEventAdaptorFactory>::create();
        pipeline.setAdaptorFactory("event", adaptorFactory);

        //Create the initial revision
        pipeline.startTransaction();
        pipeline.newEntity(command.constData(), command.size());
        pipeline.commit();

        //Get uid of written entity
        auto keys = getKeys("org.kde.pipelinetest.instance1", "event.main");
        QCOMPARE(keys.size(), 1);
        const auto key = keys.first();
        const auto uid = Akonadi2::Storage::uidFromKey(key);

        //Execute the modification
        entityFbb.Clear();
        auto modifyCommand = modifyEntityCommand(createEvent(entityFbb, "summary2"), uid, 1);
        pipeline.startTransaction();
        pipeline.modifiedEntity(modifyCommand.constData(), modifyCommand.size());
        pipeline.commit();

        //Ensure we've got the new revision with the modification
        auto buffer = getEntity("org.kde.pipelinetest.instance1", "event.main", Akonadi2::Storage::assembleKey(uid, 2));
        QVERIFY(!buffer.isEmpty());
        Akonadi2::EntityBuffer entityBuffer(buffer.data(), buffer.size());
        auto adaptor = adaptorFactory->createAdaptor(entityBuffer.entity());
        QCOMPARE(adaptor->getProperty("summary").toString(), QString("summary2"));

        //Both revisions are in the store at this point
        QCOMPARE(getKeys("org.kde.pipelinetest.instance1", "event.main").size(), 2);

        //Cleanup old revisions
        pipeline.startTransaction();
        pipeline.cleanupRevision(2);
        pipeline.commit();

        //And now only the latest revision is left
        QCOMPARE(getKeys("org.kde.pipelinetest.instance1", "event.main").size(), 1);
    }

    void testDelete()
    {
        flatbuffers::FlatBufferBuilder entityFbb;
        auto command = createEntityCommand(createEvent(entityFbb));

        //Create the initial revision
        Akonadi2::Pipeline pipeline("org.kde.pipelinetest.instance1");
        pipeline.startTransaction();
        pipeline.newEntity(command.constData(), command.size());
        pipeline.commit();

        // const auto uid = Akonadi2::Storage::uidFromKey(key);
        auto result = getKeys("org.kde.pipelinetest.instance1", "event.main");
        QCOMPARE(result.size(), 1);

        const auto uid = Akonadi2::Storage::uidFromKey(result.first());

        //Delete entity
        auto deleteCommand = deleteEntityCommand(uid, 1);
        pipeline.startTransaction();
        pipeline.deletedEntity(deleteCommand.constData(), deleteCommand.size());
        pipeline.commit();

        //We have a new revision that indicates the deletion
        QCOMPARE(getKeys("org.kde.pipelinetest.instance1", "event.main").size(), 2);

        //Cleanup old revisions
        pipeline.startTransaction();
        pipeline.cleanupRevision(2);
        pipeline.commit();

        //And all revisions are gone
        QCOMPARE(getKeys("org.kde.pipelinetest.instance1", "event.main").size(), 0);
    }
};

QTEST_MAIN(PipelineTest)
#include "pipelinetest.moc"
