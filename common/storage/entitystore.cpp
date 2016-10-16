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

#include "entitybuffer.h"
#include "log.h"
#include "typeindex.h"
#include "definitions.h"
#include "resourcecontext.h"
#include "index.h"

#include "mail.h"
#include "folder.h"
#include "event.h"

using namespace Sink;
using namespace Sink::Storage;

SINK_DEBUG_AREA("entitystore");

class EntityStore::Private {
public:
    Private(const ResourceContext &context) : resourceContext(context) {}

    ResourceContext resourceContext;
    DataStore::Transaction transaction;
    QHash<QByteArray, QSharedPointer<TypeIndex> > indexByType;

    DataStore::Transaction &getTransaction()
    {
        if (transaction) {
            return transaction;
        }

        Sink::Storage::DataStore store(Sink::storageLocation(), resourceContext.instanceId(), DataStore::ReadOnly);
        transaction = store.createTransaction(DataStore::ReadOnly);
        Q_ASSERT(transaction.validateNamedDatabases());
        return transaction;
    }

    /* template<typename T> */
    /* TypeIndex &typeIndex(const QByteArray &type) */
    /* { */
    /*     if (indexByType.contains(type)) { */
    /*         return *indexByType.value(type); */
    /*     } */
    /*     auto index = QSharedPointer<TypeIndex>::create(type); */
    /*     ApplicationDomain::TypeImplementation<T>::configureIndex(*index); */
    /*     indexByType.insert(type, index); */
    /*     return *index; */
    /* } */

    TypeIndex &typeIndex(const QByteArray &type)
    {
        /* return applyType<typeIndex>(type); */
        if (indexByType.contains(type)) {
            return *indexByType.value(type);
        }
        auto index = QSharedPointer<TypeIndex>::create(type);
        //TODO expand for all types
        /* TypeHelper<type>::configureIndex(*index); */
        // Try this: (T would i.e. become
        // TypeHelper<ApplicationDomain::TypeImplementation>::T::configureIndex(*index);
        if (type == ApplicationDomain::getTypeName<ApplicationDomain::Folder>()) {
            ApplicationDomain::TypeImplementation<ApplicationDomain::Folder>::configureIndex(*index);
        } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Mail>()) {
            ApplicationDomain::TypeImplementation<ApplicationDomain::Mail>::configureIndex(*index);
        } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Event>()) {
            ApplicationDomain::TypeImplementation<ApplicationDomain::Event>::configureIndex(*index);
        } else {
            Q_ASSERT(false);
            SinkError() << "Unkonwn type " << type;
        }
        indexByType.insert(type, index);
        return *index;
    }
};

EntityStore::EntityStore(const ResourceContext &context)
    : d(new EntityStore::Private{context})
{

}

void EntityStore::startTransaction(Sink::Storage::DataStore::AccessMode accessMode)
{
    Sink::Storage::DataStore store(Sink::storageLocation(), d->resourceContext.instanceId(), accessMode);
    d->transaction = store.createTransaction(accessMode);
    Q_ASSERT(d->transaction.validateNamedDatabases());
}

void EntityStore::commitTransaction()
{
    d->transaction.commit();
    d->transaction = Storage::DataStore::Transaction();
}

void EntityStore::abortTransaction()
{
    d->transaction.abort();
    d->transaction = Storage::DataStore::Transaction();
}

QVector<QByteArray> EntityStore::fullScan(const QByteArray &type)
{
    SinkTrace() << "Looking for : " << type;
    //The scan can return duplicate results if we have multiple revisions, so we use a set to deduplicate.
    QSet<QByteArray> keys;
    DataStore::mainDatabase(d->getTransaction(), type)
        .scan(QByteArray(),
            [&](const QByteArray &key, const QByteArray &value) -> bool {
                const auto uid = DataStore::uidFromKey(key);
                if (keys.contains(uid)) {
                    //Not something that should persist if the replay works, so we keep a message for now.
                    SinkTrace() << "Multiple revisions for key: " << key;
                }
                keys << uid;
                return true;
            },
            [](const DataStore::Error &error) { SinkWarning() << "Error during query: " << error.message; });

    SinkTrace() << "Full scan retrieved " << keys.size() << " results.";
    return keys.toList().toVector();
}

QVector<QByteArray> EntityStore::indexLookup(const QByteArray &type, const Query &query, QSet<QByteArray> &appliedFilters, QByteArray &appliedSorting)
{
    return d->typeIndex(type).query(query, appliedFilters, appliedSorting, d->getTransaction());
}

QVector<QByteArray> EntityStore::indexLookup(const QByteArray &type, const QByteArray &property, const QVariant &value)
{
    return d->typeIndex(type).lookup(property, value, d->getTransaction());
}

void EntityStore::indexLookup(const QByteArray &type, const QByteArray &property, const QVariant &value, const std::function<void(const QByteArray &uid)> &callback)
{
    auto list =  d->typeIndex(type).lookup(property, value, d->getTransaction());
    for (const auto &uid : list) {
        callback(uid);
    }
    /* Index index(type + ".index." + property, d->transaction); */
    /* index.lookup(value, [&](const QByteArray &sinkId) { */
    /*     callback(sinkId); */
    /* }, */
    /* [&](const Index::Error &error) { */
    /*     SinkWarning() << "Error in index: " <<  error.message << property; */
    /* }); */
}

void EntityStore::readLatest(const QByteArray &type, const QByteArray &uid, const std::function<void(const QByteArray &uid, const EntityBuffer &entity)> callback)
{
    auto db = DataStore::mainDatabase(d->getTransaction(), type);
    db.findLatest(uid,
        [=](const QByteArray &key, const QByteArray &value) -> bool {
            callback(DataStore::uidFromKey(key), Sink::EntityBuffer(value.data(), value.size()));
            return false;
        },
        [&](const DataStore::Error &error) { SinkWarning() << "Error during query: " << error.message << uid; });
}

void EntityStore::readLatest(const QByteArray &type, const QByteArray &uid, const std::function<void(const ApplicationDomain::ApplicationDomainType &)> callback)
{
    readLatest(type, uid, [&](const QByteArray &uid, const EntityBuffer &buffer) {
        auto adaptor = d->resourceContext.adaptorFactory(type).createAdaptor(buffer.entity());
        callback(ApplicationDomain::ApplicationDomainType{d->resourceContext.instanceId(), uid, DataStore::maxRevision(d->getTransaction()), adaptor});
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
        [&](const DataStore::Error &error) { SinkWarning() << "Error during query: " << error.message << key; });
}

void EntityStore::readEntity(const QByteArray &type, const QByteArray &uid, const std::function<void(const ApplicationDomain::ApplicationDomainType &)> callback)
{
    readEntity(type, uid, [&](const QByteArray &uid, const EntityBuffer &buffer) {
        auto adaptor = d->resourceContext.adaptorFactory(type).createAdaptor(buffer.entity());
        callback(ApplicationDomain::ApplicationDomainType{d->resourceContext.instanceId(), uid, DataStore::maxRevision(d->getTransaction()), adaptor});
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
    auto db = DataStore::mainDatabase(d->getTransaction(), type);
    db.scan("",
        [=](const QByteArray &key, const QByteArray &value) -> bool {
            auto uid = DataStore::uidFromKey(key);
            auto buffer = Sink::EntityBuffer{value.data(), value.size()};
            auto adaptor = d->resourceContext.adaptorFactory(type).createAdaptor(buffer.entity());
            callback(ApplicationDomain::ApplicationDomainType{d->resourceContext.instanceId(), uid, DataStore::maxRevision(d->getTransaction()), adaptor});
            return true;
        },
        [&](const DataStore::Error &error) { SinkWarning() << "Error during query: " << error.message; });
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
        [](const Sink::Storage::DataStore::Error &error) { SinkWarning() << "Failed to read current value from storage: " << error.message; }, true);
    return readEntity(type, Sink::Storage::DataStore::assembleKey(uid, latestRevision), callback);
}

void EntityStore::readPrevious(const QByteArray &type, const QByteArray &uid, qint64 revision, const std::function<void(const ApplicationDomain::ApplicationDomainType &)> callback)
{
    readPrevious(type, uid, revision, [&](const QByteArray &uid, const EntityBuffer &buffer) {
        auto adaptor = d->resourceContext.adaptorFactory(type).createAdaptor(buffer.entity());
        callback(ApplicationDomain::ApplicationDomainType{d->resourceContext.instanceId(), uid, DataStore::maxRevision(d->getTransaction()), adaptor});
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
    //TODO use uid index instead
    //FIXME we currently report each uid for every revision with the same uid
    auto db = DataStore::mainDatabase(d->getTransaction(), type);
    db.scan("",
        [callback](const QByteArray &key, const QByteArray &) -> bool {
            callback(Sink::Storage::DataStore::uidFromKey(key));
            return true;
        },
        [](const Sink::Storage::DataStore::Error &error) { SinkWarning() << "Failed to read current value from storage: " << error.message; });
}

bool EntityStore::contains(const QByteArray &type, const QByteArray &uid)
{
    return DataStore::mainDatabase(d->getTransaction(), type).contains(uid);
}

qint64 EntityStore::maxRevision()
{
    return DataStore::maxRevision(d->getTransaction());
}

/* DataStore::Transaction getTransaction() */
/* { */
/*     Sink::Storage::DataStore::Transaction transaction; */
/*     { */
/*         Sink::Storage::DataStore storage(Sink::storageLocation(), mResourceInstanceIdentifier); */
/*         if (!storage.exists()) { */
/*             //This is not an error if the resource wasn't started before */
/*             SinkLog() << "Store doesn't exist: " << mResourceInstanceIdentifier; */
/*             return Sink::Storage::DataStore::Transaction(); */
/*         } */
/*         storage.setDefaultErrorHandler([this](const Sink::Storage::DataStore::Error &error) { SinkWarning() << "Error during query: " << error.store << error.message; }); */
/*         transaction = storage.createTransaction(Sink::Storage::DataStore::ReadOnly); */
/*     } */

/*     //FIXME this is a temporary measure to recover from a failure to open the named databases correctly. */
/*     //Once the actual problem is fixed it will be enough to simply crash if we open the wrong database (which we check in openDatabase already). */
/*     while (!transaction.validateNamedDatabases()) { */
/*         Sink::Storage::DataStore storage(Sink::storageLocation(), mResourceInstanceIdentifier); */
/*         transaction = storage.createTransaction(Sink::Storage::DataStore::ReadOnly); */
/*     } */
/*     return transaction; */
/* } */
