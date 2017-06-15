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
#include "threadindexer.h"

#include "typeindex.h"
#include "log.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

void ThreadIndexer::updateThreadingIndex(const QByteArray &identifier, const ApplicationDomain::ApplicationDomainType &entity, Sink::Storage::DataStore::Transaction &transaction)
{
    auto messageId = entity.getProperty(Mail::MessageId::name);
    auto parentMessageId = entity.getProperty(Mail::ParentMessageId::name);
    if (messageId.toByteArray().isEmpty()) {
        SinkWarning() << "Found an email without messageId. This is illegal and threading will break. Entity id: " << identifier;
    }

    QVector<QByteArray> thread;

    //a child already registered our thread.
    thread = index().secondaryLookup<Mail::MessageId, Mail::ThreadId>(messageId);

    //If parent is already available, add to thread of parent
    if (thread.isEmpty() && parentMessageId.isValid()) {
        thread = index().secondaryLookup<Mail::MessageId, Mail::ThreadId>(parentMessageId);
        SinkTrace() << "Found parent: " << thread;
    }
    if (thread.isEmpty()) {
        thread << QUuid::createUuid().toByteArray();
        SinkTrace() << "Created a new thread: " << thread;
    }

    Q_ASSERT(!thread.isEmpty());

    if (parentMessageId.isValid()) {
        Q_ASSERT(!parentMessageId.toByteArray().isEmpty());
        //Register parent with thread for when it becomes available
        index().index<Mail::MessageId, Mail::ThreadId>(parentMessageId, thread.first(), transaction);
    }
    index().index<Mail::MessageId, Mail::ThreadId>(messageId, thread.first(), transaction);
    index().index<Mail::ThreadId, Mail::MessageId>(thread.first(), messageId, transaction);
}


void ThreadIndexer::add(const ApplicationDomain::ApplicationDomainType &entity)
{
    updateThreadingIndex(entity.identifier(), entity, transaction());
}

void ThreadIndexer::modify(const ApplicationDomain::ApplicationDomainType &old, const ApplicationDomain::ApplicationDomainType &entity)
{

}

void ThreadIndexer::remove(const ApplicationDomain::ApplicationDomainType &entity)
{

}

QMap<QByteArray, int> ThreadIndexer::databases()
{
    return {{"mail.index.messageIdthreadId", 1},
            {"mail.index.threadIdmessageId", 1}};
}

