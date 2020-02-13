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
#include <QGuiApplication>
#include <QUuid>
#include <KMime/KMime/KMimeMessage>
#include <mime/mimetreeparser/objecttreeparser.h>

#include "pipeline.h"
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

static QByteArray normalizeMessageId(const QByteArray &id)
{
    return id;
}

static QString toPlain(const QString &html)
{
    //QTextDocument has an implicit runtime dependency on QGuiApplication via the color palette.
    //If the QGuiApplication is not available we will crash (if the html contains colors).
    Q_ASSERT(QGuiApplication::instance());
    // Only get HTML content, if no plain text content
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toPlainText();
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

    const auto parentMessageIds = [&] {
        //The last is the parent
        auto references = msg->references(true)->identifiers();

        if (!references.isEmpty()) {
            QByteArrayList list;
            std::transform(references.constBegin(), references.constEnd(), std::back_inserter(list), [] (const QByteArray &id) { return normalizeMessageId(id); });
            return list;
        } else {
            auto inReplyTo = msg->inReplyTo(true)->identifiers();
            if (!inReplyTo.isEmpty()) {
                //According to RFC5256 we should ignore all but the first
                return QByteArrayList{normalizeMessageId(inReplyTo.first())};
            }
        }
        return QByteArrayList{};
    }();

    //The rest should never change, unless we didn't have the headers available initially.
    auto messageId = normalizeMessageId(msg->messageID(true)->identifier());
    if (messageId.isEmpty()) {
        //reuse an existing messageid (on modification)
        const auto existing = mail.getMessageId();
        if (existing.isEmpty()) {
            auto tmp = KMime::Message::Ptr::create();
            //Genereate a globally unique messageid that doesn't leak the local hostname
            messageId = QString{"<" + QUuid::createUuid().toString().mid(1, 36).remove('-') + "@sink>"}.toLatin1();
            tmp->messageID(true)->fromUnicodeString(messageId, "utf-8");
            SinkWarning() << "Message id is empty, generating one: " << messageId;
        } else {
            messageId = existing;
        }
    }

    mail.setExtractedMessageId(messageId);
    if (!parentMessageIds.isEmpty()) {
        mail.setExtractedParentMessageIds(parentMessageIds);
    }
    QList<QPair<QString, QString>> contentToIndex;
    contentToIndex.append({{}, msg->subject()->asUnicodeString()});

    MimeTreeParser::ObjectTreeParser otp;
    otp.parseObjectTree(msg.data());
    otp.decryptAndVerify();
    const auto plainTextContent = otp.plainTextContent();
    if (plainTextContent.isEmpty()) {
        contentToIndex.append({{}, toPlain(otp.htmlContent())});
    } else {
        contentToIndex.append({{}, plainTextContent});
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
