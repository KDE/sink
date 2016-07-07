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

void MailPropertyExtractor::updatedIndexedProperties(Sink::ApplicationDomain::Mail &mail)
{
    const auto mimeMessagePath = getFilePathFromMimeMessagePath(mail.getMimeMessagePath());
    if (mimeMessagePath.isNull()) {
        Trace() << "No mime message";
        return;
    }
    Trace() << "Updating indexed properties " << mimeMessagePath;
    QFile f(mimeMessagePath);
    if (!f.open(QIODevice::ReadOnly)) {
        Warning() << "Failed to open the file: " << mimeMessagePath;
        return;
    }
    if (!f.size()) {
        Warning() << "The file is empty.";
        return;
    }
    const auto mappedSize = qMin((qint64)8000, f.size());
    auto mapped = f.map(0, mappedSize);
    if (!mapped) {
        Warning() << "Failed to map the file: " << f.errorString();
        return;
    }

    KMime::Message *msg = new KMime::Message;
    msg->setHead(KMime::CRLFtoLF(QByteArray::fromRawData(reinterpret_cast<const char*>(mapped), mappedSize)));
    msg->parse();

    mail.setExtractedSubject(msg->subject(true)->asUnicodeString());
    mail.setExtractedSender(msg->from(true)->asUnicodeString());
    mail.setExtractedSenderName(msg->from(true)->asUnicodeString());
    mail.setExtractedDate(msg->date(true)->dateTime());
}

void MailPropertyExtractor::newEntity(Sink::ApplicationDomain::Mail &mail, Sink::Storage::Transaction &transaction)
{
    updatedIndexedProperties(mail);
}

void MailPropertyExtractor::modifiedEntity(const Sink::ApplicationDomain::Mail &oldMail, Sink::ApplicationDomain::Mail &newMail,Sink::Storage::Transaction &transaction)
{
    updatedIndexedProperties(newMail);
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
            Warning() << "Failed to create the directory: " << directory;
        }
        QFile::remove(filePath);
        QFile origFile(oldPath);
        if (!origFile.open(QIODevice::ReadWrite)) {
            Warning() << "Failed to open the original file with write rights: " << origFile.errorString();
        }
        if (!origFile.rename(filePath)) {
            Warning() << "Failed to move the file from: " << oldPath << " to " << filePath << ". " << origFile.errorString();
        }
        origFile.close();
        return filePath;
    }
    return oldPath;
}

void MimeMessageMover::newEntity(Sink::ApplicationDomain::Mail &mail, Sink::Storage::Transaction &transaction)
{
    if (!mail.getMimeMessagePath().isEmpty()) {
        mail.setMimeMessagePath(moveMessage(mail.getMimeMessagePath(), mail));
    }
}

void MimeMessageMover::modifiedEntity(const Sink::ApplicationDomain::Mail &oldMail, Sink::ApplicationDomain::Mail &newMail, Sink::Storage::Transaction &transaction)
{
    if (!newMail.getMimeMessagePath().isEmpty()) {
        newMail.setMimeMessagePath(moveMessage(newMail.getMimeMessagePath(), newMail));
    }
}

void MimeMessageMover::deletedEntity(const Sink::ApplicationDomain::Mail &mail, Sink::Storage::Transaction &transaction)
{
    QFile::remove(mail.getMimeMessagePath());
}

