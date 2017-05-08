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

SINK_DEBUG_AREA("threadindex")

using namespace Sink;
using namespace Sink::ApplicationDomain;

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


void ThreadIndexer::updateThreadingIndex(const QByteArray &identifier, const ApplicationDomain::ApplicationDomainType &entity, Sink::Storage::DataStore::Transaction &transaction)
{
    auto messageId = entity.getProperty(Mail::MessageId::name);
    auto parentMessageId = entity.getProperty(Mail::ParentMessageId::name);
    const auto subject = entity.getProperty(Mail::Subject::name);
    const auto normalizedSubject = stripOffPrefixes(subject.toString()).toUtf8();
    if (messageId.toByteArray().isEmpty()) {
        SinkWarning() << "Found an email without messageId. This is illegal and threading will break. Entity id: " << identifier;
        SinkWarning() << "Subject: " << subject;
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
        //Try to lookup the thread by subject if not empty
        if ( !normalizedSubject.isEmpty()) {
            thread = index().secondaryLookup<Mail::Subject, Mail::ThreadId>(normalizedSubject);
        }
        if (thread.isEmpty()) {
            thread << QUuid::createUuid().toByteArray();
            SinkTrace() << "Created a new thread: " << thread;
        } else {
            SinkTrace() << "Found thread by subject: " << thread;
        }
    }

    Q_ASSERT(!thread.isEmpty());

    if (parentMessageId.isValid()) {
        Q_ASSERT(!parentMessageId.toByteArray().isEmpty());
        //Register parent with thread for when it becomes available
        index().index<Mail::MessageId, Mail::ThreadId>(parentMessageId, thread.first(), transaction);
    }
    index().index<Mail::MessageId, Mail::ThreadId>(messageId, thread.first(), transaction);
    index().index<Mail::ThreadId, Mail::MessageId>(thread.first(), messageId, transaction);
    if (!normalizedSubject.isEmpty()) {
        index().index<Mail::Subject, Mail::ThreadId>(normalizedSubject, thread.first(), transaction);
    }
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
            {"mail.index.subjectthreadId", 1},
            {"mail.index.threadIdmessageId", 1}};
}

