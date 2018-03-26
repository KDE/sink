/*
 * Copyright (C) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "entitystore.h"

#include <QDir>
#include <QFile>

#include "entitybuffer.h"
#include "log.h"
#include "typeindex.h"
#include "definitions.h"
#include "resourcecontext.h"
#include "index.h"
#include "bufferutils.h"
#include "entity_generated.h"
#include "applicationdomaintype_p.h"
#include "typeimplementations.h"

using namespace Sink;
using namespace Sink::Storage;

static QMap<QByteArray, int> baseDbs()
{
    return {{"revisionType", 0},
            {"revisions", 0},
            {"uids", 0},
            {"default", 0},
            {"__flagtable", 0}};
}

template <typename T, typename First>
void mergeImpl(T &map, First f)
{
    for (auto it = f.constBegin(); it != f.constEnd(); it++) {
        map.insert(it.key(), it.value());
    }
}

template <typename T, typename First, typename ... Tail>
void mergeImpl(T &map, First f, Tail ...maps)
{
    for (auto it = f.constBegin(); it != f.constEnd(); it++) {
        map.insert(it.key(), it.value());
    }
    mergeImpl<T, Tail...>(map, maps...);
}

template <typename First, typename ... Tail>
First merge(First f, Tail ...maps)
{
    First map;
    mergeImpl(map, f, maps...);
    return map;
}

template <class T>
struct DbLayoutHelper {
    void operator()(QMap<QByteArray, int> map) const {
        mergeImpl(map, ApplicationDomain::TypeImplementation<T>::typeDatabases());
    }
};

static Sink::Storage::DbLayout dbLayout(const QByteArray &instanceId)
{
    static auto databases = [] {
        QMap<QByteArray, int> map;
        mergeImpl(map, ApplicationDomain::TypeImplementation<ApplicationDomain::Mail>::typeDatabases());
        mergeImpl(map, ApplicationDomain::TypeImplementation<ApplicationDomain::Folder>::typeDatabases());
        mergeImpl(map, ApplicationDomain::TypeImplementation<ApplicationDomain::Contact>::typeDatabases());
        mergeImpl(map, ApplicationDomain::TypeImplementation<ApplicationDomain::Addressbook>::typeDatabases());
        mergeImpl(map, ApplicationDomain::TypeImplementation<ApplicationDomain::Event>::typeDatabases());
        return merge(baseDbs(), map);
    }();
    return {instanceId, databases};
}


class EntityStore::Private {
public:
    Private(const ResourceContext &context, const Sink::Log::Context &ctx) : resourceContext(context), logCtx(ctx.subContext("entitystore"))
    {
    }

    ResourceContext resourceContext;
    DataStore::Transaction transaction;
    QHash<QByteArray, QSharedPointer<TypeIndex> > indexByType;
    Sink::Log::Context logCtx;

    bool exists()
    {
        return Sink::Storage::DataStore(Sink::storageLocation(), resourceContext.instanceId(), DataStore::ReadOnly).exists();
    }

    DataStore::Transaction &getTransaction()
    {
        if (transaction) {
            return transaction;
        }

        Sink::Storage::DataStore store(Sink::storageLocation(), dbLayout(resourceContext.instanceId()), DataStore::ReadOnly);
        transaction = store.createTransaction(DataStore::ReadOnly);
        return transaction;
    }

    template <class T>
    struct ConfigureHelper {
        void operator()(TypeIndex &arg) const {
            ApplicationDomain::TypeImplementation<T>::configure(arg);
        }
    };

    TypeIndex &cachedIndex(const QByteArray &type)
    {
        if (indexByType.contains(type)) {
            return *indexByType.value(type);
        }
        auto index = QSharedPointer<TypeIndex>::create(type, logCtx);
        TypeHelper<ConfigureHelper>{type}.template operator()<void>(*index);
        indexByType.insert(type, index);
        return *index;

    }

    TypeIndex &typeIndex(const QByteArray &type)
    {
        auto &index = cachedIndex(type);
        index.mTransaction = &transaction;
        return index;
    }

    ApplicationDomain::ApplicationDomainType createApplicationDomainType(const QByteArray &type, const QByteArray &uid, qint64 revision, const EntityBuffer &buffer)
    {
        auto adaptor = resourceContext.adaptorFactory(type).createAdaptor(buffer.entity(), &typeIndex(type));
        return ApplicationDomain::ApplicationDomainType{resourceContext.instanceId(), uid, revision, adaptor};
    }
};

EntityStore::EntityStore(const ResourceContext &context, const Log::Context &ctx)
    : d(new EntityStore::Private{context, ctx})
{

}

void EntityStore::initialize()
{
    //This function is only called in the resource code where we want to be able to write to the databse.

    //Check for the existience of the db without creating it or the envrionment.
    //This is required to be able to set the database version only in the case where we create a new database.
    if (!Storage::DataStore::exists(Sink::storageLocation(), d->resourceContext.instanceId())) {
        //The first time we open the environment we always want it to be read/write. Otherwise subsequent tries to open a write transaction will fail.
        startTransaction(Sink::Storage::DataStore::ReadWrite);
        //Create the database with the correct version if it wasn't existing before
        SinkLogCtx(d->logCtx) << "Creating resource database.";
        Storage::DataStore::setDatabaseVersion(d->transaction, Sink::latestDatabaseVersion());
    } else {
        //The first time we open the environment we always want it to be read/write. Otherwise subsequent tries to open a write transaction will fail.
        startTransaction(Sink::Storage::DataStore::ReadWrite);
    }
    commitTransaction();
}

void EntityStore::startTransaction(Sink::Storage::DataStore::AccessMode accessMode)
{
    SinkTraceCtx(d->logCtx) << "Starting transaction: " << accessMode;
    Q_ASSERT(!d->transaction);
    d->transaction = Sink::Storage::DataStore(Sink::storageLocation(), dbLayout(d->resourceContext.instanceId()), accessMode).createTransaction(accessMode);
}

void EntityStore::commitTransaction()
{
    SinkTraceCtx(d->logCtx) << "Committing transaction";

    for (const auto &type : d->indexByType.keys()) {
        d->typeIndex(type).commitTransaction();
    }

    Q_ASSERT(d->transaction);
    d->transaction.commit();
    d->transaction = {};
}

void EntityStore::abortTransaction()
{
    SinkTraceCtx(d->logCtx) << "Aborting transaction";
    d->transaction.abort();
    d->transaction = {};
}

bool EntityStore::hasTransaction() const
{
    return d->transaction;
}

bool EntityStore::add(const QByteArray &type, ApplicationDomain::ApplicationDomainType entity, bool replayToSource)
{
    if (entity.identifier().isEmpty()) {
        SinkWarningCtx(d->logCtx) << "Can't write entity with an empty identifier";
        return false;
    }

    SinkTraceCtx(d->logCtx) << "New entity " << entity;

    d->typeIndex(type).add(entity.identifier(), entity, d->transaction, d->resourceContext.instanceId());

    //The maxRevision may have changed meanwhile if the entity created sub-entities
    const qint64 newRevision = maxRevision() + 1;

    // Add metadata buffer
    flatbuffers::FlatBufferBuilder metadataFbb;
    auto metadataBuilder = MetadataBuilder(metadataFbb);
    metadataBuilder.add_revision(newRevision);
    metadataBuilder.add_operation(Operation_Creation);
    metadataBuilder.add_replayToSource(replayToSource);
    auto metadataBuffer = metadataBuilder.Finish();
    FinishMetadataBuffer(metadataFbb, metadataBuffer);

    flatbuffers::FlatBufferBuilder fbb;
    d->resourceContext.adaptorFactory(type).createBuffer(entity, fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize());

    DataStore::mainDatabase(d->transaction, type)
        .write(DataStore::assembleKey(entity.identifier(), newRevision), BufferUtils::extractBuffer(fbb),
            [&](const DataStore::Error &error) { SinkWarningCtx(d->logCtx) << "Failed to write entity" << entity.identifier() << newRevision; });
    DataStore::setMaxRevision(d->transaction, newRevision);
    DataStore::recordRevision(d->transaction, newRevision, entity.identifier(), type);
    DataStore::recordUid(d->transaction, entity.identifier(), type);
    SinkTraceCtx(d->logCtx) << "Wrote entity: " << entity.identifier() << type << newRevision;
    return true;
}

ApplicationDomain::ApplicationDomainType EntityStore::applyDiff(const QByteArray &type, const ApplicationDomain::ApplicationDomainType &current, const ApplicationDomain::ApplicationDomainType &diff, const QByteArrayList &deletions) const
{
    auto newEntity = *ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<ApplicationDomain::ApplicationDomainType>(current, current.availableProperties());

    SinkTraceCtx(d->logCtx) << "Modified entity: " << newEntity;

    // Apply diff
    //SinkTrace() << "Applying changed properties: " << changeset;
    for (const auto &property : diff.changedProperties()) {
        const auto value = diff.getProperty(property);
        if (value.isValid()) {
            //SinkTrace() << "Setting property: " << property;
            newEntity.setProperty(property, value);
        }
    }

    // Remove deletions
    for (const auto &property : deletions) {
        //SinkTrace() << "Removing property: " << property;
        newEntity.setProperty(property, QVariant());
    }
    return newEntity;
}

bool EntityStore::modify(const QByteArray &type, const ApplicationDomain::ApplicationDomainType &diff, const QByteArrayList &deletions, bool replayToSource)
{
    const auto current = readLatest(type, diff.identifier());
    if (current.identifier().isEmpty()) {
        SinkWarningCtx(d->logCtx) << "Failed to read current version: " << diff.identifier();
        return false;
    }

    auto newEntity = applyDiff(type, current, diff, deletions);
    return modify(type, current, newEntity, replayToSource);
}

bool EntityStore::modify(const QByteArray &type, const ApplicationDomain::ApplicationDomainType &current, ApplicationDomain::ApplicationDomainType newEntity, bool replayToSource)
{
    SinkTraceCtx(d->logCtx) << "Modified entity: " << newEntity;

    d->typeIndex(type).remove(current.identifier(), current, d->transaction, d->resourceContext.instanceId());
    d->typeIndex(type).add(newEntity.identifier(), newEntity, d->transaction, d->resourceContext.instanceId());

    const qint64 newRevision = DataStore::maxRevision(d->transaction) + 1;

    // Add metadata buffer
    flatbuffers::FlatBufferBuilder metadataFbb;
    {
        //We add availableProperties to account for the properties that have been changed by the preprocessors
        auto modifiedProperties = BufferUtils::toVector(metadataFbb, newEntity.changedProperties());
        auto metadataBuilder = MetadataBuilder(metadataFbb);
        metadataBuilder.add_revision(newRevision);
        metadataBuilder.add_operation(Operation_Modification);
        metadataBuilder.add_replayToSource(replayToSource);
        metadataBuilder.add_modifiedProperties(modifiedProperties);
        auto metadataBuffer = metadataBuilder.Finish();
        FinishMetadataBuffer(metadataFbb, metadataBuffer);
    }
    SinkTraceCtx(d->logCtx) << "Changed properties: " << newEntity.changedProperties();

    newEntity.setChangedProperties(newEntity.availableProperties().toSet());

    flatbuffers::FlatBufferBuilder fbb;
    d->resourceContext.adaptorFactory(type).createBuffer(newEntity, fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize());

    DataStore::mainDatabase(d->transaction, type)
        .write(DataStore::assembleKey(newEntity.identifier(), newRevision), BufferUtils::extractBuffer(fbb),
            [&](const DataStore::Error &error) { SinkWarningCtx(d->logCtx) << "Failed to write entity" << newEntity.identifier() << newRevision; });
    DataStore::setMaxRevision(d->transaction, newRevision);
    DataStore::recordRevision(d->transaction, newRevision, newEntity.identifier(), type);
    SinkTraceCtx(d->logCtx) << "Wrote modified entity: " << newEntity.identifier() << type << newRevision;
    return true;
}

bool EntityStore::remove(const QByteArray &type, const Sink::ApplicationDomain::ApplicationDomainType &current, bool replayToSource)
{
    const auto uid = current.identifier();
    if (!exists(type, uid)) {
        SinkWarningCtx(d->logCtx) << "Remove: Entity is already removed " << uid;
        return false;
    }

    d->typeIndex(type).remove(current.identifier(), current, d->transaction, d->resourceContext.instanceId());

    SinkTraceCtx(d->logCtx) << "Removed entity " << current;

    const qint64 newRevision = DataStore::maxRevision(d->transaction) + 1;

    // Add metadata buffer
    flatbuffers::FlatBufferBuilder metadataFbb;
    auto metadataBuilder = MetadataBuilder(metadataFbb);
    metadataBuilder.add_revision(newRevision);
    metadataBuilder.add_operation(Operation_Removal);
    metadataBuilder.add_replayToSource(replayToSource);
    auto metadataBuffer = metadataBuilder.Finish();
    FinishMetadataBuffer(metadataFbb, metadataBuffer);

    flatbuffers::FlatBufferBuilder fbb;
    EntityBuffer::assembleEntityBuffer(fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize(), 0, 0, 0, 0);

    DataStore::mainDatabase(d->transaction, type)
        .write(DataStore::assembleKey(uid, newRevision), BufferUtils::extractBuffer(fbb),
            [&](const DataStore::Error &error) { SinkWarningCtx(d->logCtx) << "Failed to write entity" << uid << newRevision; });
    DataStore::setMaxRevision(d->transaction, newRevision);
    DataStore::recordRevision(d->transaction, newRevision, uid, type);
    DataStore::removeUid(d->transaction, uid, type);
    return true;
}

void EntityStore::cleanupEntityRevisionsUntil(qint64 revision)
{
    const auto uid = DataStore::getUidFromRevision(d->transaction, revision);
    const auto bufferType = DataStore::getTypeFromRevision(d->transaction, revision);
    if (bufferType.isEmpty() || uid.isEmpty()) {
        SinkErrorCtx(d->logCtx) << "Failed to find revision during cleanup: " << revision;
        Q_ASSERT(false);
        return;
    }
    SinkTraceCtx(d->logCtx) << "Cleaning up revision " << revision << uid << bufferType;
    DataStore::mainDatabase(d->transaction, bufferType)
        .scan(uid,
            [&](const QByteArray &key, const QByteArray &data) -> bool {
                EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
                if (!buffer.isValid()) {
                    SinkWarningCtx(d->logCtx) << "Read invalid buffer from disk";
                } else {
                    const auto metadata = flatbuffers::GetRoot<Metadata>(buffer.metadataBuffer());
                    const qint64 rev = metadata->revision();
                    const auto isRemoval = metadata->operation() == Operation_Removal;
                    // Remove old revisions, and the current if the entity has already been removed
                    if (rev < revision || isRemoval) {
                        DataStore::removeRevision(d->transaction, rev);
                        DataStore::mainDatabase(d->transaction, bufferType).remove(key);
                    }
                    //Don't cleanup more than specified
                    if (rev >= revision) {
                        return false;
                    }
                }

                return true;
            },
            [&](const DataStore::Error &error) { SinkWarningCtx(d->logCtx) << "Error while reading: " << error.message; }, true);
    DataStore::setCleanedUpRevision(d->transaction, revision);
}

bool EntityStore::cleanupRevisions(qint64 revision)
{
    Q_ASSERT(d->exists());
    bool implicitTransaction = false;
    if (!d->transaction) {
        startTransaction(Sink::Storage::DataStore::ReadWrite);
        Q_ASSERT(d->transaction);
        implicitTransaction = true;
    }
    const auto lastCleanRevision = DataStore::cleanedUpRevision(d->transaction);
    const auto firstRevisionToCleanup = lastCleanRevision + 1;
    bool cleanupIsNecessary = firstRevisionToCleanup <= revision;
    if (cleanupIsNecessary) {
        SinkTraceCtx(d->logCtx) << "Cleaning up from " << firstRevisionToCleanup << " to " << revision;
        for (qint64 rev = firstRevisionToCleanup; rev <= revision; rev++) {
            cleanupEntityRevisionsUntil(rev);
        }
    }
    if (implicitTransaction) {
        commitTransaction();
    }
    return cleanupIsNecessary;
}

QVector<QByteArray> EntityStore::fullScan(const QByteArray &type)
{
    SinkTraceCtx(d->logCtx) << "Looking for : " << type;
    if (!d->exists()) {
        SinkTraceCtx(d->logCtx) << "Database is not existing: " << type;
        return QVector<QByteArray>();
    }
    //The scan can return duplicate results if we have multiple revisions, so we use a set to deduplicate.
    QSet<QByteArray> keys;
    DataStore::mainDatabase(d->getTransaction(), type)
        .scan(QByteArray(),
            [&](const QByteArray &key, const QByteArray &value) -> bool {
                const auto uid = DataStore::uidFromKey(key);
                if (keys.contains(uid)) {
                    //Not something that should persist if the replay works, so we keep a message for now.
                    SinkTraceCtx(d->logCtx) << "Multiple revisions for key: " << key;
                }
                keys << uid;
                return true;
            },
            [&](const DataStore::Error &error) { SinkWarningCtx(d->logCtx) << "Error during query: " << error.message; });

    SinkTraceCtx(d->logCtx) << "Full scan retrieved " << keys.size() << " results.";
    return keys.toList().toVector();
}

QVector<QByteArray> EntityStore::indexLookup(const QByteArray &type, const QueryBase &query, QSet<QByteArray> &appliedFilters, QByteArray &appliedSorting)
{
    if (!d->exists()) {
        SinkTraceCtx(d->logCtx) << "Database is not existing: " << type;
        return QVector<QByteArray>();
    }
    return d->typeIndex(type).query(query, appliedFilters, appliedSorting, d->getTransaction(), d->resourceContext.instanceId());
}

QVector<QByteArray> EntityStore::indexLookup(const QByteArray &type, const QByteArray &property, const QVariant &value)
{
    if (!d->exists()) {
        SinkTraceCtx(d->logCtx) << "Database is not existing: " << type;
        return QVector<QByteArray>();
    }
    return d->typeIndex(type).lookup(property, value, d->getTransaction());
}

void EntityStore::indexLookup(const QByteArray &type, const QByteArray &property, const QVariant &value, const std::function<void(const QByteArray &uid)> &callback)
{
    if (!d->exists()) {
        SinkTraceCtx(d->logCtx) << "Database is not existing: " << type;
        return;
    }
    auto list =  d->typeIndex(type).lookup(property, value, d->getTransaction());
    for (const auto &uid : list) {
        callback(uid);
    }
    /* Index index(type + ".index." + property, d->transaction); */
    /* index.lookup(value, [&](const QByteArray &sinkId) { */
    /*     callback(sinkId); */
    /* }, */
    /* [&](const Index::Error &error) { */
    /*     SinkWarningCtx(d->logCtx) << "Error in index: " <<  error.message << property; */
    /* }); */
}

void EntityStore::readLatest(const QByteArray &type, const QByteArray &uid, const std::function<void(const QByteArray &uid, const EntityBuffer &entity)> callback)
{
    Q_ASSERT(d);
    auto db = DataStore::mainDatabase(d->getTransaction(), type);
    db.findLatest(uid,
        [=](const QByteArray &key, const QByteArray &value) {
            callback(DataStore::uidFromKey(key), Sink::EntityBuffer(value.data(), value.size()));
        },
        [&](const DataStore::Error &error) { SinkWarningCtx(d->logCtx) << "Error during query: " << error.message << uid; });
}

void EntityStore::readLatest(const QByteArray &type, const QByteArray &uid, const std::function<void(const ApplicationDomain::ApplicationDomainType &)> callback)
{
    readLatest(type, uid, [&](const QByteArray &uid, const EntityBuffer &buffer) {
        //TODO cache max revision for the duration of the transaction.
        callback(d->createApplicationDomainType(type, uid, DataStore::maxRevision(d->getTransaction()), buffer));
    });
}

void EntityStore::readLatest(const QByteArray &type, const QByteArray &uid, const std::function<void(const ApplicationDomain::ApplicationDomainType &, Sink::Operation)> callback)
{
    readLatest(type, uid, [&](const QByteArray &uid, const EntityBuffer &buffer) {
        //TODO cache max revision for the duration of the transaction.
        callback(d->createApplicationDomainType(type, uid, DataStore::maxRevision(d->getTransaction()), buffer), buffer.operation());
    });
}

ApplicationDomain::ApplicationDomainType EntityStore::readLatest(const QByteArray &type, const QByteArray &uid)
{
    ApplicationDomain::ApplicationDomainType dt;
    readLatest(type, uid, [&](const ApplicationDomain::ApplicationDomainType &entity) {
        dt = entity;
    });
    return dt;
}

void EntityStore::readEntity(const QByteArray &type, const QByteArray &key, const std::function<void(const QByteArray &uid, const EntityBuffer &entity)> callback)
{
    auto db = DataStore::mainDatabase(d->getTransaction(), type);
    db.scan(key,
        [=](const QByteArray &key, const QByteArray &value) -> bool {
            callback(DataStore::uidFromKey(key), Sink::EntityBuffer(value.data(), value.size()));
            return false;
        },
        [&](const DataStore::Error &error) { SinkWarningCtx(d->logCtx) << "Error during query: " << error.message << key; });
}

void EntityStore::readEntity(const QByteArray &type, const QByteArray &uid, const std::function<void(const ApplicationDomain::ApplicationDomainType &)> callback)
{
    readEntity(type, uid, [&](const QByteArray &uid, const EntityBuffer &buffer) {
        callback(d->createApplicationDomainType(type, uid, DataStore::maxRevision(d->getTransaction()), buffer));
    });
}

ApplicationDomain::ApplicationDomainType EntityStore::readEntity(const QByteArray &type, const QByteArray &uid)
{
    ApplicationDomain::ApplicationDomainType dt;
    readEntity(type, uid, [&](const ApplicationDomain::ApplicationDomainType &entity) {
        dt = entity;
    });
    return dt;
}


void EntityStore::readAll(const QByteArray &type, const std::function<void(const ApplicationDomain::ApplicationDomainType &entity)> &callback)
{
    readAllUids(type, [&] (const QByteArray &uid) {
        readLatest(type, uid, callback);
    });
}

void EntityStore::readRevisions(qint64 baseRevision, const QByteArray &expectedType, const std::function<void(const QByteArray &key)> &callback)
{
    qint64 revisionCounter = baseRevision;
    const qint64 topRevision = DataStore::maxRevision(d->getTransaction());
    // Spit out the revision keys one by one.
    while (revisionCounter <= topRevision) {
        const auto uid = DataStore::getUidFromRevision(d->getTransaction(), revisionCounter);
        const auto type = DataStore::getTypeFromRevision(d->getTransaction(), revisionCounter);
        // SinkTrace() << "Revision" << *revisionCounter << type << uid;
        Q_ASSERT(!uid.isEmpty());
        Q_ASSERT(!type.isEmpty());
        if (type != expectedType) {
            // Skip revision
            revisionCounter++;
            continue;
        }
        const auto key = DataStore::assembleKey(uid, revisionCounter);
        revisionCounter++;
        callback(key);
    }
}

void EntityStore::readPrevious(const QByteArray &type, const QByteArray &uid, qint64 revision, const std::function<void(const QByteArray &uid, const EntityBuffer &entity)> callback)
{
    auto db = DataStore::mainDatabase(d->getTransaction(), type);
    qint64 latestRevision = 0;
    db.scan(uid,
        [&latestRevision, revision](const QByteArray &key, const QByteArray &) -> bool {
            const auto foundRevision = Sink::Storage::DataStore::revisionFromKey(key);
            if (foundRevision < revision && foundRevision > latestRevision) {
                latestRevision = foundRevision;
            }
            return true;
        },
        [&](const Sink::Storage::DataStore::Error &error) { SinkWarningCtx(d->logCtx) << "Failed to read current value from storage: " << error.message; }, true);
    readEntity(type, Sink::Storage::DataStore::assembleKey(uid, latestRevision), callback);
}

void EntityStore::readPrevious(const QByteArray &type, const QByteArray &uid, qint64 revision, const std::function<void(const ApplicationDomain::ApplicationDomainType &)> callback)
{
    readPrevious(type, uid, revision, [&](const QByteArray &uid, const EntityBuffer &buffer) {
        callback(d->createApplicationDomainType(type, uid, DataStore::maxRevision(d->getTransaction()), buffer));
    });
}

ApplicationDomain::ApplicationDomainType EntityStore::readPrevious(const QByteArray &type, const QByteArray &uid, qint64 revision)
{
    ApplicationDomain::ApplicationDomainType dt;
    readPrevious(type, uid, revision, [&](const ApplicationDomain::ApplicationDomainType &entity) {
        dt = entity;
    });
    return dt;
}

void EntityStore::readAllUids(const QByteArray &type, const std::function<void(const QByteArray &uid)> callback)
{
    DataStore::getUids(type, d->getTransaction(), callback);
}

bool EntityStore::contains(const QByteArray &type, const QByteArray &uid)
{
    return DataStore::mainDatabase(d->getTransaction(), type).contains(uid);
}

bool EntityStore::exists(const QByteArray &type, const QByteArray &uid)
{
    bool found = false;
    bool alreadyRemoved = false;
    DataStore::mainDatabase(d->transaction, type)
        .findLatest(uid,
            [&found, &alreadyRemoved](const QByteArray &key, const QByteArray &data) {
                auto entity = GetEntity(data.data());
                if (entity && entity->metadata()) {
                    auto metadata = GetMetadata(entity->metadata()->Data());
                    found = true;
                    if (metadata->operation() == Operation_Removal) {
                        alreadyRemoved = true;
                    }
                }
            },
            [&](const DataStore::Error &error) { SinkWarningCtx(d->logCtx) << "Failed to read old revision from storage: " << error.message; });
    if (!found) {
        SinkTraceCtx(d->logCtx) << "Remove: Failed to find entity " << uid;
        return false;
    }
    if (alreadyRemoved) {
        SinkTraceCtx(d->logCtx) << "Remove: Entity is already removed " << uid;
        return false;
    }
    return true;
}


qint64 EntityStore::maxRevision()
{
    if (!d->exists()) {
        SinkTraceCtx(d->logCtx) << "Database is not existing.";
        return 0;
    }
    return DataStore::maxRevision(d->getTransaction());
}

Sink::Log::Context EntityStore::logContext() const
{
    return d->logCtx;
}
