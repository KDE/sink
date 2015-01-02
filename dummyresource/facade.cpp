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
#include <common/entitybuffer.h>

using namespace DummyCalendar;
using namespace flatbuffers;

//This will become a generic implementation that simply takes the resource buffer and local buffer pointer
class DummyEventAdaptor : public Akonadi2::Domain::BufferAdaptor
{
public:
    DummyEventAdaptor()
        : BufferAdaptor()
    {

    }

    void setProperty(const QString &key, const QVariant &value)
    {
        if (mResourceMapper->mWriteAccessors.contains(key)) {
            // mResourceMapper.setProperty(key, value, mResourceBuffer);
        } else {
            // mLocalMapper.;
        }
    }

    virtual QVariant getProperty(const QString &key) const
    {
        if (mResourceBuffer && mResourceMapper->mReadAccessors.contains(key)) {
            return mResourceMapper->getProperty(key, mResourceBuffer);
        } else if (mLocalBuffer) {
            return mLocalMapper->getProperty(key, mLocalBuffer);
        }
        return QVariant();
    }

    Akonadi2::Domain::Buffer::Event const *mLocalBuffer;
    DummyEvent const *mResourceBuffer;

    QSharedPointer<PropertyMapper<Akonadi2::Domain::Buffer::Event> > mLocalMapper;
    QSharedPointer<PropertyMapper<DummyEvent> > mResourceMapper;
};

template<>
QSharedPointer<Akonadi2::Domain::BufferAdaptor> DomainTypeAdaptorFactory<typename Akonadi2::Domain::Event, typename Akonadi2::Domain::Buffer::Event, DummyEvent>::createAdaptor(const Akonadi2::Entity &entity)
{
    DummyEvent const *resourceBuffer = 0;
    if (auto resourceData = entity.resource()) {
        flatbuffers::Verifier verifyer(resourceData->Data(), resourceData->size());
        if (VerifyDummyEventBuffer(verifyer)) {
            resourceBuffer = GetDummyEvent(resourceData);
        }
    }

    Akonadi2::Metadata const *metadataBuffer = 0;
    if (auto metadataData = entity.metadata()) {
        flatbuffers::Verifier verifyer(metadataData->Data(), metadataData->size());
        if (Akonadi2::VerifyMetadataBuffer(verifyer)) {
            metadataBuffer = Akonadi2::GetMetadata(metadataData);
        }
    }

    Akonadi2::Domain::Buffer::Event const *localBuffer = 0;
    if (auto localData = entity.local()) {
        flatbuffers::Verifier verifyer(localData->Data(), localData->size());
        if (Akonadi2::Domain::Buffer::VerifyEventBuffer(verifyer)) {
            localBuffer = Akonadi2::Domain::Buffer::GetEvent(localData);
        }
    }

    auto adaptor = QSharedPointer<DummyEventAdaptor>::create();
    adaptor->mLocalBuffer = localBuffer;
    adaptor->mResourceBuffer = resourceBuffer;
    adaptor->mResourceMapper = mResourceMapper;
    adaptor->mLocalMapper = mLocalMapper;
    return adaptor;
}

DummyResourceFacade::DummyResourceFacade()
    : Akonadi2::StoreFacade<Akonadi2::Domain::Event>(),
    mResourceAccess(new Akonadi2::ResourceAccess("org.kde.dummy")),
    mFactory(new DomainTypeAdaptorFactory<Akonadi2::Domain::Event, Akonadi2::Domain::Buffer::Event, DummyCalendar::DummyEvent>())
{
    auto mapper = QSharedPointer<PropertyMapper<DummyEvent> >::create();
    mapper->mReadAccessors.insert("summary", [](DummyEvent const *buffer) -> QVariant {
        return QString::fromStdString(buffer->summary()->c_str());
    });
    mFactory->mResourceMapper = mapper;
}

DummyResourceFacade::~DummyResourceFacade()
{
}

void DummyResourceFacade::create(const Akonadi2::Domain::Event &domainObject)
{
    //Create message buffer and send to resource
}

void DummyResourceFacade::modify(const Akonadi2::Domain::Event &domainObject)
{
    //Create message buffer and send to resource
}

void DummyResourceFacade::remove(const Akonadi2::Domain::Event &domainObject)
{
    //Create message buffer and send to resource
}

//Key.value property map using enum or strings with qvariant, or rather typesafe API?
//typesafe is a shitload more work that we can avoid
//
//The Event base implementaiton could take a pointer to a single property mapper,
//and a void pointer to the mmapped region. => event is copyable and stack allocatable and we avoid large amounts of heap allocated objects
//-The mapper should in this case live in the other thread
//-default property mapper implementation can answer "is property X supported?"
//-how do we free/munmap the data if we don't know when no one references it any longer? => no munmap needed, but read transaction to keep pointer alive
//-we could bind the lifetime to the query
//=> perhaps do heap allocate and use smart pointer?
//


static std::function<bool(const std::string &key, DummyEvent const *buffer)> prepareQuery(const Akonadi2::Query &query)
{
    //Compose some functions to make query matching fast.
    //This way we can process the query once, and convert all values into something that can be compared quickly
    std::function<bool(const std::string &key, DummyEvent const *buffer)> preparedQuery;
    if (!query.ids.isEmpty()) {
        //Match by id
        //TODO: for id's a direct lookup would be way faster

        //We convert the id's to std::string so we don't have to convert each key during the scan. (This runs only once, and the query will be run for every key)
        //Probably a premature optimization, but perhaps a useful technique to be investigated.
        QVector<std::string> ids;
        for (const auto &id : query.ids) {
            ids << id.toStdString();
        }
        preparedQuery = [ids](const std::string &key, DummyEvent const *buffer) {
            if (ids.contains(key)) {
                return true;
            }
            return false;
        };
    } else {
        //Match everything
        preparedQuery = [](const std::string &key, DummyEvent const *buffer) {
            return true;
        };
    }
    return preparedQuery;
}

void DummyResourceFacade::synchronizeResource(const std::function<void()> &continuation)
{
    //TODO check if a sync is necessary
    //TODO Only sync what was requested
    //TODO timeout
    mResourceAccess->open();
    mResourceAccess->synchronizeResource(continuation);
}

void DummyResourceFacade::load(const Akonadi2::Query &query, const std::function<void(const Akonadi2::Domain::Event::Ptr &)> &resultCallback, const std::function<void()> &completeCallback)
{
    qDebug() << "load called";

    synchronizeResource([=]() {
        qDebug() << "sync complete";
        //Now that the sync is complete we can execute the query
        const auto preparedQuery = prepareQuery(query);

        auto storage = QSharedPointer<Akonadi2::Storage>::create(Akonadi2::Store::storageLocation(), "org.kde.dummy");

        qDebug() << "executing query";
        //We start a transaction explicitly that we'll leave open so the values can be read.
        //The transaction will be closed automatically once the storage object is destroyed.
        storage->startTransaction(Akonadi2::Storage::ReadOnly);
        //Because we have no indexes yet, we always do a full scan
        storage->scan("", [=](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {

            //Skip internals
            if (QByteArray::fromRawData(static_cast<char*>(keyValue), keySize).startsWith("__internal")) {
                return true;
            }

            //Extract buffers
            Akonadi2::EntityBuffer buffer(dataValue, dataSize);

            DummyEvent const *resourceBuffer = 0;
            if (auto resourceData = buffer.resourceBuffer()) {
                flatbuffers::Verifier verifyer(resourceData->Data(), resourceData->size());
                if (VerifyDummyEventBuffer(verifyer)) {
                    resourceBuffer = GetDummyEvent(resourceData);
                }
            }

            Akonadi2::Metadata const *metadataBuffer = 0;
            if (auto metadataData = buffer.metadataBuffer()) {
                flatbuffers::Verifier verifyer(metadataData->Data(), metadataData->size());
                if (Akonadi2::VerifyMetadataBuffer(verifyer)) {
                    metadataBuffer = Akonadi2::GetMetadata(metadataData);
                }
            }

            if (!resourceBuffer || !metadataBuffer) {
                qWarning() << "invalid buffer " << QString::fromStdString(std::string(static_cast<char*>(keyValue), keySize));
                return true;
            }

            //We probably only want to create all buffers after the scan
            //TODO use adapter for query and scan?
            if (preparedQuery && preparedQuery(std::string(static_cast<char*>(keyValue), keySize), resourceBuffer)) {
                qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
                auto adaptor = mFactory->createAdaptor(buffer.entity());
                auto event = QSharedPointer<Akonadi2::Domain::Event>::create("org.kde.dummy", QString::fromUtf8(static_cast<char*>(keyValue), keySize), revision, adaptor);
                resultCallback(event);
            }
            return true;
        });
        completeCallback();
    });
}

