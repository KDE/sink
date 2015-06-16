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
#include "common/resultset.h"
#include "common/domain/event.h"
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
    : Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>("org.kde.dummy.instance1", QSharedPointer<DummyEventAdaptorFactory>::create())
{
}

DummyResourceFacade::~DummyResourceFacade()
{
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

static void readValue(const QSharedPointer<Akonadi2::Storage> &storage, const QByteArray &key, const std::function<void(const Akonadi2::ApplicationDomain::Event::Ptr &)> &resultCallback, const QSharedPointer<DomainTypeAdaptorFactoryInterface<Akonadi2::ApplicationDomain::Event> > &adaptorFactory)
{
    scan(storage, key, [=](const QByteArray &key, const Akonadi2::Entity &entity, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local, Akonadi2::Metadata const *metadataBuffer) {
        qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
        //This only works for a 1:1 mapping of resource to domain types.
        //Not i.e. for tags that are stored as flags in each entity of an imap store.
        //additional properties that don't have a 1:1 mapping (such as separately stored tags),
        //could be added to the adaptor
        auto event = QSharedPointer<Akonadi2::ApplicationDomain::Event>::create("org.kde.dummy.instance1", key, revision, adaptorFactory->createAdaptor(entity));
        resultCallback(event);
        return true;
    });
}

static ResultSet getResultSet(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::Storage> &storage, const QSharedPointer<DomainTypeAdaptorFactoryInterface<Akonadi2::ApplicationDomain::Event> > &adaptorFactory)
{
    QSet<QByteArray> appliedFilters;
    ResultSet resultSet = Akonadi2::ApplicationDomain::TypeImplementation<Akonadi2::ApplicationDomain::Event>::queryIndexes(query, "org.kde.dummy.instance1", appliedFilters);
    const auto remainingFilters = query.propertyFilter.keys().toSet() - appliedFilters;

    //We do a full scan if there were no indexes available to create the initial set.
    //TODO use a result set with an iterator, to read values on demand
    if (appliedFilters.isEmpty()) {
        QVector<QByteArray> keys;
        scan(storage, QByteArray(), [=, &keys](const QByteArray &key, const Akonadi2::Entity &entity, DummyEvent const *buffer, Akonadi2::ApplicationDomain::Buffer::Event const *local, Akonadi2::Metadata const *metadataBuffer) {
            keys << key;
            return true;
        });
        resultSet = ResultSet(keys);
    }

    auto filter = [remainingFilters, query](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &event) -> bool {
        for (const auto &filterProperty : remainingFilters) {
            //TODO implement other comparison operators than equality
            if (event->getProperty(filterProperty) != query.propertyFilter.value(filterProperty)) {
                return false;
            }
        }
        return true;
    };

    auto resultSetPtr = QSharedPointer<ResultSet>::create(resultSet);

    //Read through the source values and return whatever matches the filter
    std::function<bool(std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &)>)> generator = [resultSetPtr, storage, adaptorFactory, filter](std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &)> callback) -> bool {
        while (resultSetPtr->next()) {
            Akonadi2::ApplicationDomain::Event::Ptr event;
            readValue(storage, resultSetPtr->id(), [filter, callback](const Akonadi2::ApplicationDomain::Event::Ptr &event) {
                if (filter(event)) {
                    callback(event);
                }
            }, adaptorFactory);
        }
        return false;
    };
    return ResultSet(generator);
}

//TODO generalize
KAsync::Job<qint64> DummyResourceFacade::load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> > &resultProvider, qint64 oldRevision, qint64 newRevision)
{
    return KAsync::start<qint64>([=]() {
        auto storage = QSharedPointer<Akonadi2::Storage>::create(Akonadi2::Store::storageLocation(), "org.kde.dummy.instance1");
        storage->setDefaultErrorHandler([](const Akonadi2::Storage::Error &error) {
            Warning() << "Error during query: " << error.store << error.message;
        });

        storage->startTransaction(Akonadi2::Storage::ReadOnly);
        //TODO start transaction on indexes as well
        const qint64 revision = storage->maxRevision();

        auto resultSet = getResultSet(query, storage, mDomainTypeAdaptorFactory);

        // TODO only emit changes and don't replace everything
        resultProvider->clear();
        auto resultCallback = std::bind(&Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr>::add, resultProvider, std::placeholders::_1);
        while(resultSet.next([resultCallback](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &value) -> bool {
            resultCallback(Akonadi2::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<Akonadi2::ApplicationDomain::Event>(value));
        })){};
        storage->abortTransaction();
        return revision;
    });
}

