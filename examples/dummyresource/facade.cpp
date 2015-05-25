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
#include "domainadaptor.h"
#include <common/entitybuffer.h>
#include <common/index.h>
#include <common/log.h>

using namespace DummyCalendar;
using namespace flatbuffers;


DummyResourceFacade::DummyResourceFacade()
    : Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>("org.kde.dummy", QSharedPointer<DummyEventAdaptorFactory>::create())
{
}

DummyResourceFacade::~DummyResourceFacade()
{
}

static std::function<bool(const std::string &key, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local)> prepareQuery(const Akonadi2::Query &query)
{
    //Compose some functions to make query matching fast.
    //This way we can process the query once, and convert all values into something that can be compared quickly
    std::function<bool(const std::string &key, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local)> preparedQuery;
    if (!query.ids.isEmpty()) {
        //Match by id
        //TODO: for id's a direct lookup would be way faster

        //We convert the id's to std::string so we don't have to convert each key during the scan. (This runs only once, and the query will be run for every key)
        //Probably a premature optimization, but perhaps a useful technique to be investigated.
        QVector<std::string> ids;
        for (const auto &id : query.ids) {
            ids << id.toStdString();
        }
        preparedQuery = [ids](const std::string &key, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local) {
            if (ids.contains(key)) {
                return true;
            }
            return false;
        };
    } else if (!query.propertyFilter.isEmpty()) {
        if (query.propertyFilter.contains("uid")) {
            const QByteArray uid = query.propertyFilter.value("uid").toByteArray();
            preparedQuery = [uid](const std::string &key, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local) {
                if (local && local->uid() && (QByteArray::fromRawData(local->uid()->c_str(), local->uid()->size()) == uid)) {
                    return true;
                }
                return false;
            };
        }
    } else {
        //Match everything
        preparedQuery = [](const std::string &key, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local) {
            return true;
        };
    }
    return preparedQuery;
}

static void scan(const QSharedPointer<Akonadi2::Storage> &storage, const QByteArray &key, std::function<bool(const QByteArray &key, const Akonadi2::Entity &entity, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local, Akonadi2::Metadata const *metadata)> callback)
{
    storage->scan(key, [=](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
        //Skip internals
        if (Akonadi2::Storage::isInternalKey(keyValue, keySize)) {
            return true;
        }

        //Extract buffers
        Akonadi2::EntityBuffer buffer(dataValue, dataSize);

        const auto resourceBuffer = Akonadi2::EntityBuffer::readBuffer<DummyEvent>(buffer.entity().resource());
        const auto localBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::ApplicationDomain::Buffer::Event>(buffer.entity().local());
        const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(buffer.entity().metadata());

        if ((!resourceBuffer && !localBuffer) || !metadataBuffer) {
            qWarning() << "invalid buffer " << QByteArray::fromRawData(static_cast<char*>(keyValue), keySize);
            return true;
        }
        return callback(QByteArray::fromRawData(static_cast<char*>(keyValue), keySize), buffer.entity(), resourceBuffer, localBuffer, metadataBuffer);
    },
    [](const Akonadi2::Storage::Error &error) {
        qWarning() << "Error during query: " << error.message;
    });
}

void DummyResourceFacade::readValue(const QSharedPointer<Akonadi2::Storage> &storage, const QByteArray &key, const std::function<void(const Akonadi2::ApplicationDomain::Event::Ptr &)> &resultCallback)
{
    scan(storage, key, [=](const QByteArray &key, const Akonadi2::Entity &entity, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local, Akonadi2::Metadata const *metadataBuffer) {
        qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
        //This only works for a 1:1 mapping of resource to domain types.
        //Not i.e. for tags that are stored as flags in each entity of an imap store.
        //additional properties that don't have a 1:1 mapping (such as separately stored tags),
        //could be added to the adaptor
        auto event = QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("org.kde.dummy", key, revision, mDomainTypeAdaptorFactory->createAdaptor(entity));
        resultCallback(event);
        return true;
    });
}

//TODO this should return an iterator to the result set so we can lazy load
static QVector<QByteArray> getResultSet(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::Storage> &storage)
{
    //Now that the sync is complete we can execute the query
    const auto preparedQuery = prepareQuery(query);

    //Index lookups
    //TODO query standard indexes
    QVector<QByteArray> keys;
    if (query.propertyFilter.contains("uid")) {
        static Index uidIndex(Akonadi2::Store::storageLocation(), "org.kde.dummy.index.uid", Akonadi2::Storage::ReadOnly);
        uidIndex.lookup(query.propertyFilter.value("uid").toByteArray(), [&](const QByteArray &value) {
            keys << value;
        },
        [](const Index::Error &error) {
            Warning() << "Error in index: " <<  error.message;
        });
    }

    //Scan for where we don't have an index
    if (keys.isEmpty()) {
        scan(storage, QByteArray(), [preparedQuery, &keys](const QByteArray &key, const Akonadi2::Entity &entity, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local, Akonadi2::Metadata const *metadataBuffer) {
            //TODO use adapter for query and scan?
            if (preparedQuery && preparedQuery(std::string(key.constData(), key.size()), buffer, local)) {
                keys << key;
            }
            return true;
        });
    }

    return keys;
}

KAsync::Job<qint64> DummyResourceFacade::load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> > &resultProvider, qint64 oldRevision, qint64 newRevision)
{
    return KAsync::start<qint64>([=]() {
        auto storage = QSharedPointer<Akonadi2::Storage>::create(Akonadi2::Store::storageLocation(), "org.kde.dummy");
        storage->setDefaultErrorHandler([](const Akonadi2::Storage::Error &error) {
            Warning() << "Error during query: " << error.store << error.message;
        });

        storage->startTransaction(Akonadi2::Storage::ReadOnly);
        //TODO start transaction on indexes as well
        const qint64 revision = storage->maxRevision();

        auto resultSet = getResultSet(query, storage);

        // TODO only emit changes and don't replace everything
        resultProvider->clear();
        auto resultCallback = std::bind(&Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr>::add, resultProvider, std::placeholders::_1);
        for (const auto &key : resultSet) {
            readValue(storage, key, [resultCallback](const Akonadi2::ApplicationDomain::Event::Ptr &event) {
                //We create an in-memory copy because the result provider will store the value, and the result we get back is only valid during the callback
                resultCallback(Akonadi2::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<Akonadi2::ApplicationDomain::Event>(event));
            });
        }
        storage->abortTransaction();
        return revision;
    });
}

