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

#include "mailpreprocessor.h"

#include <QFile>
#include <QDir>
#include <QTextDocument>
#include <KMime/KMime/KMimeMessage>

#include "pipeline.h"
#include "fulltextindex.h"
#include "definitions.h"
#include "applicationdomaintype.h"

using namespace Sink;

static Sink::ApplicationDomain::Mail::Contact getContact(const KMime::Headers::Generics::MailboxList *header)
{
    const auto name = header->displayNames().isEmpty() ? QString() : header->displayNames().first();
    const auto address = header->addresses().isEmpty() ? QString() : header->addresses().first();
    return Sink::ApplicationDomain::Mail::Contact{name, address};
}

static QList<Sink::ApplicationDomain::Mail::Contact> getContactList(const KMime::Headers::Generics::AddressList *header)
{
    QList<Sink::ApplicationDomain::Mail::Contact> list;
    for (const auto &mb : header->mailboxes()) {
        list << Sink::ApplicationDomain::Mail::Contact{mb.name(), mb.address()};
    }
    return list;
}

static QList<QPair<QString, QString>> processPart(KMime::Content* content)
{
    if (KMime::Headers::ContentType* type = content->contentType(false)) {
        if (type->isMultipart() && !type->isSubtype("encrypted")) {
            QList<QPair<QString, QString>> list;
            for (const auto c : content->contents()) {
                list << processPart(c);
            }
            return list;
        } else if (type->isHTMLText()) {
            // Only get HTML content, if no plain text content
            QTextDocument doc;
            doc.setHtml(content->decodedText());
            return {{{}, {doc.toPlainText()}}};
        } else if (type->isEmpty()) {
            return {{{}, {content->decodedText()}}};
        }
    }
    return {};
}

void MailPropertyExtractor::updatedIndexedProperties(Sink::ApplicationDomain::Mail &mail, const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }
    auto msg = KMime::Message::Ptr(new KMime::Message);
    msg->setContent(KMime::CRLFtoLF(data));
    msg->parse();
    if (!msg) {
        return;
    }

    mail.setExtractedSubject(msg->subject(true)->asUnicodeString());
    mail.setExtractedSender(getContact(msg->from(true)));
    mail.setExtractedTo(getContactList(msg->to(true)));
    mail.setExtractedCc(getContactList(msg->cc(true)));
    mail.setExtractedBcc(getContactList(msg->bcc(true)));
    mail.setExtractedDate(msg->date(true)->dateTime());

    //Ensure the mssageId is unique.
    //If there already is one with the same id we'd have to assign a new message id, which probably doesn't make any sense.

    //The last is the parent
    auto references = msg->references(true)->identifiers();

    //The first is the parent
    auto inReplyTo = msg->inReplyTo(true)->identifiers();
    QByteArray parentMessageId;
    if (!references.isEmpty()) {
        parentMessageId = references.last();
        //TODO we could use the rest of the references header to complete the ancestry in case we have missing parents.
    } else {
        if (!inReplyTo.isEmpty()) {
            //According to RFC5256 we should ignore all but the first
            parentMessageId = inReplyTo.first();
        }
    }

    //The rest should never change, unless we didn't have the headers available initially.
    auto messageId = msg->messageID(true)->identifier();
    if (messageId.isEmpty()) {
        //reuse an existing messageis (on modification)
        auto existing = mail.getMessageId();
        if (existing.isEmpty()) {
            auto tmp = KMime::Message::Ptr::create();
            auto header = tmp->messageID(true);
            header->generate("kube.kde.org");
            messageId = header->as7BitString();
            SinkWarning() << "Message id is empty, generating one: " << messageId;
        } else {
            messageId = existing;
        }
    }

    mail.setExtractedMessageId(messageId);
    if (!parentMessageId.isEmpty()) {
        mail.setExtractedParentMessageId(parentMessageId);
    }
    QList<QPair<QString, QString>> contentToIndex;
    contentToIndex.append({{}, msg->subject()->asUnicodeString()});
    if (KMime::Content* mainBody = msg->mainBodyPart("text/plain")) {
        contentToIndex.append({{}, mainBody->decodedText()});
    } else {
        contentToIndex << processPart(msg.data());
    }
    const auto sender = mail.getSender();
    contentToIndex.append({{}, sender.name});
    contentToIndex.append({{}, sender.emailAddress});
    for (const auto &c : mail.getTo()) {
        contentToIndex.append({{}, c.name});
        contentToIndex.append({{}, c.emailAddress});
    }
    for (const auto &c : mail.getCc()) {
        contentToIndex.append({{}, c.name});
        contentToIndex.append({{}, c.emailAddress});
    }
    for (const auto &c : mail.getBcc()) {
        contentToIndex.append({{}, c.name});
        contentToIndex.append({{}, c.emailAddress});
    }

    //Prepare content for indexing;
    mail.setProperty("index", QVariant::fromValue(contentToIndex));
}

void MailPropertyExtractor::newEntity(Sink::ApplicationDomain::Mail &mail)
{
    updatedIndexedProperties(mail, mail.getMimeMessage());
}

void MailPropertyExtractor::modifiedEntity(const Sink::ApplicationDomain::Mail &oldMail, Sink::ApplicationDomain::Mail &newMail)
{
    updatedIndexedProperties(newMail, newMail.getMimeMessage());
}
