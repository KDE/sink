/*
 *   Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include "mail.h"

#include <QVector>
#include <QByteArray>
#include <QString>
#include <QMutex>
#include <QMutexLocker>

#include "../resultset.h"
#include "../index.h"
#include "../storage.h"
#include "../log.h"
#include "../propertymapper.h"
#include "../query.h"
#include "../definitions.h"
#include "../typeindex.h"
#include "entitybuffer.h"
#include "entity_generated.h"

#include "mail_generated.h"

SINK_DEBUG_AREA("mail");

static QMutex sMutex;

using namespace Sink;
using namespace Sink::ApplicationDomain;

static TypeIndex &getIndex()
{
    QMutexLocker locker(&sMutex);
    static TypeIndex *index = 0;
    if (!index) {
        index = new TypeIndex("mail");
        index->addProperty<QByteArray>(Mail::Uid::name);
        index->addProperty<QByteArray>(Mail::Sender::name);
        index->addProperty<QByteArray>(Mail::SenderName::name);
        index->addProperty<QString>(Mail::Subject::name);
        index->addProperty<QDateTime>(Mail::Date::name);
        index->addProperty<QByteArray>(Mail::Folder::name);
        index->addPropertyWithSorting<QByteArray, QDateTime>(Mail::Folder::name, Mail::Date::name);
        index->addProperty<QByteArray>(Mail::MessageId::name);
        index->addProperty<QByteArray>(Mail::ParentMessageId::name);
    }
    return *index;
}

static void updateThreadingIndex(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction)
{
    auto messageId = bufferAdaptor.getProperty(Mail::MessageId::name).toByteArray();
    auto parentMessageId = bufferAdaptor.getProperty(Mail::ParentMessageId::name).toByteArray();

    Index msgIdIndex("msgId", transaction);
    Index msgIdThreadIdIndex("msgIdThreadId", transaction);

    //Add the message to the index
    Q_ASSERT(msgIdIndex.lookup(messageId).isEmpty());
    msgIdIndex.add(messageId, identifier);

    //If parent is already available, add to thread of parent
    QByteArray thread;
    if (!parentMessageId.isEmpty() && !msgIdIndex.lookup(parentMessageId).isEmpty()) {
        thread = msgIdThreadIdIndex.lookup(parentMessageId);
        msgIdThreadIdIndex.add(messageId, thread);
    } else {
        thread = QUuid::createUuid().toByteArray();
        if (!parentMessageId.isEmpty()) {
            //Register parent with thread for when it becomes available
            msgIdThreadIdIndex.add(parentMessageId, thread);
        }
    }
    Q_ASSERT(!thread.isEmpty());
    msgIdThreadIdIndex.add(messageId, thread);

    //Look for parentMessageId and resolve to local id if available
}

void TypeImplementation<Mail>::index(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction)
{
    SinkTrace() << "Indexing " << identifier;
    getIndex().add(identifier, bufferAdaptor, transaction);
    updateThreadingIndex(identifier, bufferAdaptor, transaction);
}

void TypeImplementation<Mail>::removeIndex(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction)
{
    getIndex().remove(identifier, bufferAdaptor, transaction);
}

QSharedPointer<ReadPropertyMapper<TypeImplementation<Mail>::Buffer> > TypeImplementation<Mail>::initializeReadPropertyMapper()
{
    auto propertyMapper = QSharedPointer<ReadPropertyMapper<Buffer> >::create();
    propertyMapper->addMapping<Mail::Uid, Buffer>(&Buffer::uid);
    propertyMapper->addMapping<Mail::Sender, Buffer>(&Buffer::sender);
    propertyMapper->addMapping<Mail::SenderName, Buffer>(&Buffer::senderName);
    propertyMapper->addMapping<Mail::Subject, Buffer>(&Buffer::subject);
    propertyMapper->addMapping<Mail::Date, Buffer>(&Buffer::date);
    propertyMapper->addMapping<Mail::Unread, Buffer>(&Buffer::unread);
    propertyMapper->addMapping<Mail::Important, Buffer>(&Buffer::important);
    propertyMapper->addMapping<Mail::Folder, Buffer>(&Buffer::folder);
    propertyMapper->addMapping<Mail::MimeMessage, Buffer>(&Buffer::mimeMessage);
    propertyMapper->addMapping<Mail::Draft, Buffer>(&Buffer::draft);
    propertyMapper->addMapping<Mail::Trash, Buffer>(&Buffer::trash);
    propertyMapper->addMapping<Mail::Sent, Buffer>(&Buffer::sent);
    propertyMapper->addMapping<Mail::MessageId, Buffer>(&Buffer::messageId);
    propertyMapper->addMapping<Mail::ParentMessageId, Buffer>(&Buffer::parentMessageId);
    return propertyMapper;
}

QSharedPointer<WritePropertyMapper<TypeImplementation<Mail>::BufferBuilder> > TypeImplementation<Mail>::initializeWritePropertyMapper()
{
    auto propertyMapper = QSharedPointer<WritePropertyMapper<BufferBuilder> >::create();

    propertyMapper->addMapping<Mail::Uid>(&BufferBuilder::add_uid);
    propertyMapper->addMapping<Mail::Sender>(&BufferBuilder::add_sender);
    propertyMapper->addMapping<Mail::SenderName>(&BufferBuilder::add_senderName);
    propertyMapper->addMapping<Mail::Subject>(&BufferBuilder::add_subject);
    propertyMapper->addMapping<Mail::Date>(&BufferBuilder::add_date);
    propertyMapper->addMapping<Mail::Unread>(&BufferBuilder::add_unread);
    propertyMapper->addMapping<Mail::Important>(&BufferBuilder::add_important);
    propertyMapper->addMapping<Mail::Folder>(&BufferBuilder::add_folder);
    propertyMapper->addMapping<Mail::MimeMessage>(&BufferBuilder::add_mimeMessage);
    propertyMapper->addMapping<Mail::Draft>(&BufferBuilder::add_draft);
    propertyMapper->addMapping<Mail::Trash>(&BufferBuilder::add_trash);
    propertyMapper->addMapping<Mail::Sent>(&BufferBuilder::add_sent);
    propertyMapper->addMapping<Mail::MessageId>(&BufferBuilder::add_messageId);
    propertyMapper->addMapping<Mail::ParentMessageId>(&BufferBuilder::add_parentMessageId);
    return propertyMapper;
}

class ThreadedDataStoreQuery : public DataStoreQuery
{
public:
    typedef QSharedPointer<ThreadedDataStoreQuery> Ptr;
    using DataStoreQuery::DataStoreQuery;

protected:
    ResultSet postSortFilter(ResultSet &resultSet) Q_DECL_OVERRIDE
    {
        auto query = mQuery;
        if (query.threadLeaderOnly) {
            auto rootCollection = QSharedPointer<QMap<QByteArray, QDateTime>>::create();
            auto filter = [this, query, rootCollection](const QByteArray &uid, const Sink::EntityBuffer &entity) -> bool {
                //TODO lookup thread
                //if we got thread already in the result set compare dates and if newer replace
                //else insert

                const auto messageId = getProperty(entity.entity(), ApplicationDomain::Mail::MessageId::name).toByteArray();

                Index msgIdIndex("msgId", mTransaction);
                Index msgIdThreadIdIndex("msgIdThreadId", mTransaction);
                auto thread = msgIdThreadIdIndex.lookup(messageId);
                SinkTrace() << "MsgId: " << messageId << " Thread: " << thread << getProperty(entity.entity(), ApplicationDomain::Mail::Date::name).toDateTime();

                if (rootCollection->contains(thread)) {
                    auto date = rootCollection->value(thread);
                    //The mail we have in our result already is newer, so we can ignore this one
                    if (date > getProperty(entity.entity(), ApplicationDomain::Mail::Date::name).toDateTime()) {
                        return false;
                    }
                    qWarning() << "############################################################################";
                    qWarning() << "Found a newer mail, remove the old one";
                    qWarning() << "############################################################################";
                }
                rootCollection->insert(thread, getProperty(entity.entity(), ApplicationDomain::Mail::Date::name).toDateTime());
                return true;
            };
            return createFilteredSet(resultSet, filter);
        } else {
            return resultSet;
        }
    }
};

DataStoreQuery::Ptr TypeImplementation<Mail>::prepareQuery(const Sink::Query &query, Sink::Storage::Transaction &transaction)
{
    if (query.threadLeaderOnly) {
        auto mapper = initializeReadPropertyMapper();
        return ThreadedDataStoreQuery::Ptr::create(query, ApplicationDomain::getTypeName<Mail>(), transaction, getIndex(), [mapper](const Sink::Entity &entity, const QByteArray &property) {

            const auto localBuffer = Sink::EntityBuffer::readBuffer<Buffer>(entity.local());
            return mapper->getProperty(property, localBuffer);
        });

    } else {
        auto mapper = initializeReadPropertyMapper();
        return DataStoreQuery::Ptr::create(query, ApplicationDomain::getTypeName<Mail>(), transaction, getIndex(), [mapper](const Sink::Entity &entity, const QByteArray &property) {

            const auto localBuffer = Sink::EntityBuffer::readBuffer<Buffer>(entity.local());
            return mapper->getProperty(property, localBuffer);
        });
    }
}

