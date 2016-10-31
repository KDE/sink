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

SINK_DEBUG_AREA("mailpreprocessor")

QString MailPropertyExtractor::getFilePathFromMimeMessagePath(const QString &s) const
{
    return s;
}

struct MimeMessageReader {
    MimeMessageReader(const QString &mimeMessagePath)
        : f(mimeMessagePath),
        mapped(0),
        mappedSize(0)
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
        mappedSize = qMin((qint64)8000, f.size());
        mapped = f.map(0, mappedSize);
        if (!mapped) {
            SinkWarning() << "Failed to map the file: " << f.errorString();
            return;
        }
    }

    KMime::Message::Ptr mimeMessage()
    {
        if (!mapped) {
            return KMime::Message::Ptr();
        }
        Q_ASSERT(mapped);
        Q_ASSERT(mappedSize);
        auto msg = KMime::Message::Ptr(new KMime::Message);
        msg->setHead(KMime::CRLFtoLF(QByteArray::fromRawData(reinterpret_cast<const char*>(mapped), mappedSize)));
        msg->parse();
        return msg;
    }

    QFile f;
    uchar *mapped;
    qint64 mappedSize;
};

static void updatedIndexedProperties(Sink::ApplicationDomain::Mail &mail, KMime::Message::Ptr msg)
{
    mail.setExtractedSubject(msg->subject(true)->asUnicodeString());
    /* mail.setExtractedSender(msg->from(true)->asUnicodeString()); */
    /* mail.setExtractedSenderName(msg->from(true)->asUnicodeString()); */
    mail.setExtractedDate(msg->date(true)->dateTime());

    //The rest should never change, unless we didn't have the headers available initially.
    auto messageId = msg->messageID(true)->identifier();

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


MimeMessageMover::MimeMessageMover() : Sink::EntityPreprocessor<ApplicationDomain::Mail>()
{
}

QString MimeMessageMover::moveMessage(const QString &oldPath, const Sink::ApplicationDomain::Mail &mail)
{
    const auto directory = Sink::resourceStorageLocation(resourceInstanceIdentifier());
    const auto filePath = directory + "/" + mail.identifier();
    if (oldPath != filePath) {
        if (!QDir().mkpath(directory)) {
            SinkWarning() << "Failed to create the directory: " << directory;
        }
        QFile::remove(filePath);
        QFile origFile(oldPath);
        if (!origFile.open(QIODevice::ReadWrite)) {
            SinkWarning() << "Failed to open the original file with write rights: " << origFile.errorString();
        }
        if (!origFile.rename(filePath)) {
            SinkWarning() << "Failed to move the file from: " << oldPath << " to " << filePath << ". " << origFile.errorString();
        }
        origFile.close();
        return filePath;
    }
    return oldPath;
}

void MimeMessageMover::newEntity(Sink::ApplicationDomain::Mail &mail)
{
    if (!mail.getMimeMessagePath().isEmpty()) {
        mail.setMimeMessagePath(moveMessage(mail.getMimeMessagePath(), mail));
    }
}

void MimeMessageMover::modifiedEntity(const Sink::ApplicationDomain::Mail &oldMail, Sink::ApplicationDomain::Mail &newMail)
{
    if (!newMail.getMimeMessagePath().isEmpty()) {
        newMail.setMimeMessagePath(moveMessage(newMail.getMimeMessagePath(), newMail));
    }
}

void MimeMessageMover::deletedEntity(const Sink::ApplicationDomain::Mail &mail)
{
    QFile::remove(mail.getMimeMessagePath());
}

