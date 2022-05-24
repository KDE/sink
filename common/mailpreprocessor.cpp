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
#include <KMime/KMime/Headers>
#include <mime/mimetreeparser/objecttreeparser.h>

#include "pipeline.h"
#include "definitions.h"
#include "applicationdomaintype.h"

using namespace Sink;

static QString getString(const KMime::Headers::Base *header, const QString &defaultValue = {})
{
    if (!header) {
        return defaultValue;
    }
    return header->asUnicodeString() ;
}

static QDateTime getDate(const KMime::Headers::Base *header)
{
    if (!header) {
        return QDateTime::currentDateTimeUtc();
    }
    return static_cast<const KMime::Headers::Date*>(header)->dateTime();
}

static Sink::ApplicationDomain::Mail::Contact fromMailbox(const KMime::Types::Mailbox &mb)
{
    return Sink::ApplicationDomain::Mail::Contact{mb.name(), mb.address()};
}

static Sink::ApplicationDomain::Mail::Contact getContact(const KMime::Headers::Base *h)
{
    if (!h) {
        return {};
    }
    const auto header = static_cast<const KMime::Headers::Generics::MailboxList*>(h);
    const auto mb = header->mailboxes().isEmpty() ? KMime::Types::Mailbox{} : header->mailboxes().first();
    return fromMailbox(mb);
}

static QList<Sink::ApplicationDomain::Mail::Contact> getContactList(const KMime::Headers::Base *h)
{
    if (!h) {
        return {};
    }
    const auto header = static_cast<const KMime::Headers::Generics::AddressList*>(h);
    QList<Sink::ApplicationDomain::Mail::Contact> list;
    for (const auto &mb : header->mailboxes()) {
        list << fromMailbox(mb);
    }
    return list;
}

static QVector<QByteArray> getIdentifiers(const KMime::Headers::Base *h)
{
    if (!h) {
        return {};
    }
    return static_cast<const KMime::Headers::Generics::Ident*>(h)->identifiers();
}

static QByteArray getIdentifier(const KMime::Headers::Base *h)
{
    if (!h) {
        return {};
    }
    return static_cast<const KMime::Headers::Generics::SingleIdent*>(h)->identifier();
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
        //Always set a dummy subject and date, so we can find the message
        //In test we sometimes pre-set the extracted date though, so we check that first.
        if (mail.getSubject().isEmpty()) {
            mail.setExtractedSubject("Error: Empty message");
        }
        if (!mail.getDate().isValid()) {
            mail.setExtractedDate(QDateTime::currentDateTimeUtc());
        }
        return;
    }
    MimeTreeParser::ObjectTreeParser otp;
    otp.parseObjectTree(data);
    otp.decryptAndVerify();

    const auto partList = otp.collectContentParts();
    const auto part = [&] () -> MimeTreeParser::MessagePartPtr {
        if (!partList.isEmpty()) {
            return partList[0];
        }
        //Extract headers also if there are only attachment parts.
        return  otp.parsedPart();
    }();
    Q_ASSERT(part);

    mail.setExtractedSubject(getString(part->header(KMime::Headers::Subject::staticType()), "Error: No subject"));
    mail.setExtractedSender(getContact(part->header(KMime::Headers::From::staticType())));
    mail.setExtractedTo(getContactList(part->header(KMime::Headers::To::staticType())));
    mail.setExtractedCc(getContactList(part->header(KMime::Headers::Cc::staticType())));
    mail.setExtractedBcc(getContactList(part->header(KMime::Headers::Bcc::staticType())));
    mail.setExtractedDate(getDate(part->header(KMime::Headers::Date::staticType())));

    const auto parentMessageIds = [&] {
        //The last is the parent
        const auto references = getIdentifiers(part->header(KMime::Headers::References::staticType()));

        if (!references.isEmpty()) {
            QByteArrayList list;
            std::transform(references.constBegin(), references.constEnd(), std::back_inserter(list), [] (const QByteArray &id) { return normalizeMessageId(id); });
            return list;
        } else {
            const auto inReplyTo = getIdentifiers(part->header(KMime::Headers::InReplyTo::staticType()));
            if (!inReplyTo.isEmpty()) {
                //According to RFC5256 we should ignore all but the first
                return QByteArrayList{normalizeMessageId(inReplyTo.first())};
            }
        }
        return QByteArrayList{};
    }();

    //The rest should never change, unless we didn't have the headers available initially.
    auto messageId = normalizeMessageId(getIdentifier(part->header(KMime::Headers::MessageID::staticType())));
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
    const auto subject = getString(part->header(KMime::Headers::Subject::staticType()));
    contentToIndex.append({{"subject"}, subject});

    const auto plainTextContent = otp.plainTextContent();
    if (plainTextContent.isEmpty()) {
        contentToIndex.append({{}, toPlain(otp.htmlContent())});
    } else {
        contentToIndex.append({{}, plainTextContent});
    }

    const auto sender = mail.getSender();
    contentToIndex.append({{"sender"}, sender.name});
    contentToIndex.append({{"sender"}, sender.emailAddress});
    for (const auto &c : mail.getTo()) {
        contentToIndex.append({{"recipients"}, c.name});
        contentToIndex.append({{"recipients"}, c.emailAddress});
    }
    for (const auto &c : mail.getCc()) {
        contentToIndex.append({{"recipients"}, c.name});
        contentToIndex.append({{"recipients"}, c.emailAddress});
    }
    for (const auto &c : mail.getBcc()) {
        contentToIndex.append({{"recipients"}, c.name});
        contentToIndex.append({{"recipients"}, c.emailAddress});
    }

    //Prepare content for indexing;
    mail.setProperty("index", QVariant::fromValue(contentToIndex));
    mail.setProperty("indexDate", QVariant::fromValue(mail.getDate()));
}

void MailPropertyExtractor::newEntity(Sink::ApplicationDomain::Mail &mail)
{
    updatedIndexedProperties(mail, mail.getMimeMessage());
}

void MailPropertyExtractor::modifiedEntity(const Sink::ApplicationDomain::Mail &oldMail, Sink::ApplicationDomain::Mail &newMail)
{
    updatedIndexedProperties(newMail, newMail.getMimeMessage());
}
