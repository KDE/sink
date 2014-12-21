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

using namespace DummyCalendar;
using namespace flatbuffers;

DummyResourceFacade::DummyResourceFacade()
    : Akonadi2::StoreFacade<Akonadi2::Domain::Event>(),
    mResourceAccess(new Akonadi2::ResourceAccess("org.kde.dummy"))
{
    // connect(mResourceAccess.data(), &ResourceAccess::ready, this, onReadyChanged);
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
class DummyEventAdaptor : public Akonadi2::Domain::Event
{
public:
    DummyEventAdaptor(const QString &resource, const QString &identifier, qint64 revision)
        :Akonadi2::Domain::Event(resource, identifier, revision)
    {
    }

    //TODO
    // void setProperty(const QString &key, const QVariant &value)
    // {
    //     //Record changes to send to resource?
    //     //The buffer is readonly
    // }

    virtual QVariant getProperty(const QString &key) const
    {
        if (key == "summary") {
            //FIXME how do we check availability for on-demand request?
            return QString::fromStdString(buffer->summary()->c_str());
        }
        return QVariant();
    }

    //Data is read-only
    DummyEvent const *buffer;

    //Keep query alive so values remain valid
    QSharedPointer<Akonadi2::Storage> storage;
};

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
        //Now that the sync is complete we can execute the query
        const auto preparedQuery = prepareQuery(query);

        auto storage = QSharedPointer<Akonadi2::Storage>::create(Akonadi2::Store::storageLocation(), "org.kde.dummy");

        qDebug() << "executing query";
        //We start a transaction explicitly that we'll leave open so the values can be read.
        //The transaction will be closed automatically once the storage object is destroyed.
        storage->startTransaction(Akonadi2::Storage::ReadOnly);
        //Because we have no indexes yet, we always do a full scan
        storage->scan("", [=](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
            //TODO read the three buffers
            qDebug() << QString::fromStdString(std::string(static_cast<char*>(keyValue), keySize));
            auto eventBuffer = GetDummyEvent(dataValue);
            if (preparedQuery && preparedQuery(std::string(static_cast<char*>(keyValue), keySize), eventBuffer)) {
                //TODO set proper revision
                qint64 revision = 0;
                auto event = QSharedPointer<DummyEventAdaptor>::create("org.kde.dummy", QString::fromUtf8(static_cast<char*>(keyValue), keySize), revision);
                event->buffer = eventBuffer;
                event->storage = storage;
                resultCallback(event);
            }
            return true;
        });
        completeCallback();
    });
}

