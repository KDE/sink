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
#include <KMime/KMime/KMimeMessage>

#include "pipeline.h"
#include "definitions.h"
#include "applicationdomaintype.h"

using namespace Sink;

QString MailPropertyExtractor::getFilePathFromMimeMessagePath(const QString &s) const
{
    return s;
}

struct MimeMessageReader {
    MimeMessageReader(const QString &mimeMessagePath)
        : f(mimeMessagePath),
        mapped(0)
    {
        if (mimeMessagePath.isNull()) {
            SinkTrace() << "No mime message";
            return;
        }
        SinkTrace() << "Updating indexed properties " << mimeMessagePath;
        if (!f.open(QIODevice::ReadOnly)) {
            SinkWarning() << "Failed to open the file: " << mimeMessagePath;
            return;
        }
        if (!f.size()) {
            SinkWarning() << "The file is empty.";
            return;
        }
        mapped = f.map(0, f.size());
        if (!mapped) {
            SinkWarning() << "Failed to map the file: " << f.errorString();
            return;
        }
    }

    KMime::Message::Ptr mimeMessage()
    {
        if (!mapped) {
            return {};
        }
        QByteArray result;
        //Seek for end of headers
        const auto content = QByteArray::fromRawData(reinterpret_cast<const char*>(mapped), f.size());
        int pos = content.indexOf("\r\n\r\n", 0);
        int offset = 2;
        if (pos < 0) {
            pos = content.indexOf("\n\n", 0);
            offset = 1;
        }
        if (pos > -1) {
            const auto header = content.left(pos + offset);    //header *must* end with "\n" !!
            auto msg = KMime::Message::Ptr(new KMime::Message);
            msg->setHead(KMime::CRLFtoLF(header));
            msg->parse();
            return msg;
        }
        SinkWarning() << "Failed to find end of headers" << content;
        return {};
    }

    QFile f;
    uchar *mapped;
};

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

static void updatedIndexedProperties(Sink::ApplicationDomain::Mail &mail, KMime::Message::Ptr msg)
{
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
}

void MailPropertyExtractor::newEntity(Sink::ApplicationDomain::Mail &mail)
{
    MimeMessageReader mimeMessageReader(getFilePathFromMimeMessagePath(mail.getMimeMessagePath()));
    auto msg = mimeMessageReader.mimeMessage();
    if (msg) {
        updatedIndexedProperties(mail, msg);
    }
}

void MailPropertyExtractor::modifiedEntity(const Sink::ApplicationDomain::Mail &oldMail, Sink::ApplicationDomain::Mail &newMail)
{
    MimeMessageReader mimeMessageReader(getFilePathFromMimeMessagePath(newMail.getMimeMessagePath()));
    auto msg = mimeMessageReader.mimeMessage();
    if (msg) {
        updatedIndexedProperties(newMail, msg);
    }
}

