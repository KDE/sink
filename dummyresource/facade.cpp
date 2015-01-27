/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include "facade.h"

#include <QDebug>
#include <functional>

#include "common/resourceaccess.h"
#include "common/commands.h"
#include "dummycalendar_generated.h"
#include "event_generated.h"
#include "entity_generated.h"
#include "metadata_generated.h"
#include "createentity_generated.h"
#include "domainadaptor.h"
#include <common/entitybuffer.h>
#include <common/index.h>

using namespace DummyCalendar;
using namespace flatbuffers;

DummyResourceFacade::DummyResourceFacade()
    : Akonadi2::StoreFacade<Akonadi2::Domain::Event>(),
    mResourceAccess(new Akonadi2::ResourceAccess("org.kde.dummy")),
    mFactory(new DummyEventAdaptorFactory)
{
}

DummyResourceFacade::~DummyResourceFacade()
{
}

Async::Job<void> DummyResourceFacade::create(const Akonadi2::Domain::Event &domainObject)
{
    flatbuffers::FlatBufferBuilder entityFbb;
    mFactory->createBuffer(domainObject, entityFbb);

    flatbuffers::FlatBufferBuilder fbb;
    //This is the resource buffer type and not the domain type
    auto type = fbb.CreateString("event");
    auto delta = fbb.CreateVector<uint8_t>(entityFbb.GetBufferPointer(), entityFbb.GetSize());
    auto location = Akonadi2::Commands::CreateCreateEntity(fbb, type, delta);
    Akonadi2::Commands::FinishCreateEntityBuffer(fbb, location);
    mResourceAccess->open();
    return mResourceAccess->sendCommand(Akonadi2::Commands::CreateEntityCommand, fbb);
}

Async::Job<void> DummyResourceFacade::modify(const Akonadi2::Domain::Event &domainObject)
{
    //Create message buffer and send to resource
    return Async::null<void>();
}

Async::Job<void> DummyResourceFacade::remove(const Akonadi2::Domain::Event &domainObject)
{
    //Create message buffer and send to resource
    return Async::null<void>();
}

static std::function<bool(const std::string &key, DummyEvent const *buffer, Akonadi2::Domain::Buffer::Event const *local)> prepareQuery(const Akonadi2::Query &query)
{
    //Compose some functions to make query matching fast.
    //This way we can process the query once, and convert all values into something that can be compared quickly
    std::function<bool(const std::string &key, DummyEvent const *buffer, Akonadi2::Domain::Buffer::Event const *local)> preparedQuery;
    if (!query.ids.isEmpty()) {
        //Match by id
        //TODO: for id's a direct lookup would be way faster

        //We convert the id's to std::string so we don't have to convert each key during the scan. (This runs only once, and the query will be run for every key)
        //Probably a premature optimization, but perhaps a useful technique to be investigated.
        QVector<std::string> ids;
        for (const auto &id : query.ids) {
            ids << id.toStdString();
        }
        preparedQuery = [ids](const std::string &key, DummyEvent const *buffer, Akonadi2::Domain::Buffer::Event const *local) {
            if (ids.contains(key)) {
                return true;
            }
            return false;
        };
    } else if (!query.propertyFilter.isEmpty()) {
        if (query.propertyFilter.contains("uid")) {
            const QByteArray uid = query.propertyFilter.value("uid").toByteArray();
            preparedQuery = [uid](const std::string &key, DummyEvent const *buffer, Akonadi2::Domain::Buffer::Event const *local) {
                if (local && local->uid() && (QByteArray::fromRawData(local->uid()->c_str(), local->uid()->size()) == uid)) {
                    qDebug() << "uid match";
                    return true;
                }
                return false;
            };
        }
    } else {
        //Match everything
        preparedQuery = [](const std::string &key, DummyEvent const *buffer, Akonadi2::Domain::Buffer::Event const *local) {
            return true;
        };
    }
    return preparedQuery;
}

Async::Job<void> DummyResourceFacade::synchronizeResource(bool sync, bool processAll)
{
    //TODO check if a sync is necessary
    //TODO Only sync what was requested
    //TODO timeout

    if (sync || processAll) {
        return Async::start<void>([=](Async::Future<void> &future) {
            mResourceAccess->open();
            mResourceAccess->synchronizeResource(sync, processAll).then<void>([&future](Async::Future<void> &f) {
                future.setFinished();
                f.setFinished();
            }).exec();
        });
    }
    return Async::null<void>();
}

void DummyResourceFacade::readValue(QSharedPointer<Akonadi2::Storage> storage, const QByteArray &key, const std::function<void(const Akonadi2::Domain::Event::Ptr &)> &resultCallback, std::function<bool(const std::string &key, DummyEvent const *buffer, Akonadi2::Domain::Buffer::Event const *local)> preparedQuery)
{
    storage->scan(key.data(), key.size(), [=](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {

        //Skip internals
        if (Akonadi2::Storage::isInternalKey(keyValue, keySize)) {
            return true;
        }

        //Extract buffers
        Akonadi2::EntityBuffer buffer(dataValue, dataSize);

        DummyEvent const *resourceBuffer = 0;
        if (auto resourceData = buffer.entity().resource()) {
            flatbuffers::Verifier verifyer(resourceData->Data(), resourceData->size());
            if (VerifyDummyEventBuffer(verifyer)) {
                resourceBuffer = GetDummyEvent(resourceData->Data());
            }
        }

        Akonadi2::Domain::Buffer::Event const *localBuffer = 0;
        if (auto localData = buffer.entity().local()) {
            flatbuffers::Verifier verifyer(localData->Data(), localData->size());
            if (Akonadi2::Domain::Buffer::VerifyEventBuffer(verifyer)) {
                localBuffer = Akonadi2::Domain::Buffer::GetEvent(localData->Data());
            }
        }

        Akonadi2::Metadata const *metadataBuffer = 0;
        if (auto metadataData = buffer.entity().metadata()) {
            flatbuffers::Verifier verifyer(metadataData->Data(), metadataData->size());
            if (Akonadi2::VerifyMetadataBuffer(verifyer)) {
                metadataBuffer = Akonadi2::GetMetadata(metadataData->Data());
            }
        }

        if (!resourceBuffer || !metadataBuffer) {
            qWarning() << "invalid buffer " << QString::fromStdString(std::string(static_cast<char*>(keyValue), keySize));
            return true;
        }

        //We probably only want to create all buffers after the scan
        //TODO use adapter for query and scan?
        if (preparedQuery && preparedQuery(std::string(static_cast<char*>(keyValue), keySize), resourceBuffer, localBuffer)) {
            qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
            //This only works for a 1:1 mapping of resource to domain types.
            //Not i.e. for tags that are stored as flags in each entity of an imap store.
            auto adaptor = mFactory->createAdaptor(buffer.entity());
            //TODO only copy requested properties
            auto memoryAdaptor = QSharedPointer<Akonadi2::Domain::MemoryBufferAdaptor>::create(*adaptor);
            auto event = QSharedPointer<Akonadi2::Domain::Event>::create("org.kde.dummy", QString::fromUtf8(static_cast<char*>(keyValue), keySize), revision, memoryAdaptor);
            resultCallback(event);
        }
        return true;
    },
    [](const Akonadi2::Storage::Error &error) {
        qWarning() << "Error during query: " << QString::fromStdString(error.message);
    });
}

Async::Job<void> DummyResourceFacade::load(const Akonadi2::Query &query, const std::function<void(const Akonadi2::Domain::Event::Ptr &)> &resultCallback)
{
    return synchronizeResource(query.syncOnDemand, query.processAll).then<void>([=](Async::Future<void> &future) {
        //Now that the sync is complete we can execute the query
        const auto preparedQuery = prepareQuery(query);

        auto storage = QSharedPointer<Akonadi2::Storage>::create(Akonadi2::Store::storageLocation(), "org.kde.dummy");

        QVector<QByteArray> keys;
        if (query.propertyFilter.contains("uid")) {
            static Index uidIndex(Akonadi2::Store::storageLocation(), "org.kde.dummy.index.uid", Akonadi2::Storage::ReadOnly);
            uidIndex.lookup(query.propertyFilter.value("uid").toByteArray(), [&](const QByteArray &value) {
                keys << value;
            },
            [](const Index::Error &error) {
                qWarning() << "Error in index: " <<  QString::fromStdString(error.message);
            });
        }

        //We start a transaction explicitly that we'll leave open so the values can be read.
        //The transaction will be closed automatically once the storage object is destroyed.
        storage->startTransaction(Akonadi2::Storage::ReadOnly);
        if (keys.isEmpty()) {
            qDebug() << "full scan";
            readValue(storage, QByteArray(), resultCallback, preparedQuery);
        } else {
            for (const auto &key : keys) {
                readValue(storage, key, resultCallback, preparedQuery);
            }
        }
        future.setFinished();
    });
}

