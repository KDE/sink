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
#include "datastorequery.h"
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

static QString stripOffPrefixes(const QString &subject)
{
    //TODO this hardcoded list is probably not good enough (especially regarding internationalization)
    //TODO this whole routine, including internationalized re/fwd ... should go into some library.
    //We'll require the same for generating reply/forward subjects in kube
    static QStringList defaultReplyPrefixes = QStringList() << QLatin1String("Re\\s*:")
                                                            << QLatin1String("Re\\[\\d+\\]:")
                                                            << QLatin1String("Re\\d+:");

    static QStringList defaultForwardPrefixes = QStringList() << QLatin1String("Fwd:")
                                                              << QLatin1String("FW:");

    QStringList replyPrefixes; // = GlobalSettings::self()->replyPrefixes();
    if (replyPrefixes.isEmpty()) {
        replyPrefixes = defaultReplyPrefixes;
    }

    QStringList forwardPrefixes; // = GlobalSettings::self()->forwardPrefixes();
    if (forwardPrefixes.isEmpty()) {
        forwardPrefixes = defaultReplyPrefixes;
    }

    const QStringList prefixRegExps = replyPrefixes + forwardPrefixes;

    // construct a big regexp that
    // 1. is anchored to the beginning of str (sans whitespace)
    // 2. matches at least one of the part regexps in prefixRegExps
    const QString bigRegExp = QString::fromLatin1("^(?:\\s+|(?:%1))+\\s*").arg(prefixRegExps.join(QLatin1String(")|(?:")));

    static QString regExpPattern;
    static QRegExp regExp;

    regExp.setCaseSensitivity(Qt::CaseInsensitive);
    if (regExpPattern != bigRegExp) {
        // the prefixes have changed, so update the regexp
        regExpPattern = bigRegExp;
        regExp.setPattern(regExpPattern);
    }

    if(regExp.isValid()) {
        QString tmp = subject;
        if (regExp.indexIn( tmp ) == 0) {
            return tmp.remove(0, regExp.matchedLength());
        }
    } else {
        SinkWarning() << "bigRegExp = \""
                   << bigRegExp << "\"\n"
                   << "prefix regexp is invalid!";
    }

    return subject;
}


static void updateThreadingIndex(const QByteArray &identifier, const BufferAdaptor &bufferAdaptor, Sink::Storage::Transaction &transaction)
{
    auto messageId = bufferAdaptor.getProperty(Mail::MessageId::name).toByteArray();
    auto parentMessageId = bufferAdaptor.getProperty(Mail::ParentMessageId::name).toByteArray();
    auto subject = bufferAdaptor.getProperty(Mail::Subject::name).toString();

    Index msgIdIndex("msgId", transaction);
    Index msgIdThreadIdIndex("msgIdThreadId", transaction);
    Index subjectThreadIdIndex("subjectThreadId", transaction);

    //Add the message to the index
    Q_ASSERT(msgIdIndex.lookup(messageId).isEmpty());
    msgIdIndex.add(messageId, identifier);

    auto normalizedSubject = stripOffPrefixes(subject).toUtf8();

    QByteArray thread;
    //If parent is already available, add to thread of parent
    if (!parentMessageId.isEmpty() && !msgIdIndex.lookup(parentMessageId).isEmpty()) {
        thread = msgIdThreadIdIndex.lookup(parentMessageId);
        msgIdThreadIdIndex.add(messageId, thread);
        subjectThreadIdIndex.add(normalizedSubject, thread);
    } else {
        //Try to lookup the thread by subject:
        thread = subjectThreadIdIndex.lookup(normalizedSubject);
        if (!thread.isEmpty()) {
            msgIdThreadIdIndex.add(messageId, thread);
        } else {
            thread = QUuid::createUuid().toByteArray();
            subjectThreadIdIndex.add(normalizedSubject, thread);
            if (!parentMessageId.isEmpty()) {
                //Register parent with thread for when it becomes available
                msgIdThreadIdIndex.add(parentMessageId, thread);
            }
        }
    }
    Q_ASSERT(!thread.isEmpty());
    msgIdThreadIdIndex.add(messageId, thread);
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


DataStoreQuery::Ptr TypeImplementation<Mail>::prepareQuery(const Sink::Query &query, Sink::Storage::Transaction &transaction)
{


        auto mapper = initializeReadPropertyMapper();
        return DataStoreQuery::Ptr::create(query, ApplicationDomain::getTypeName<Mail>(), transaction, getIndex(), [mapper](const Sink::Entity &entity, const QByteArray &property) {

            const auto localBuffer = Sink::EntityBuffer::readBuffer<Buffer>(entity.local());
            return mapper->getProperty(property, localBuffer);
        });
}

