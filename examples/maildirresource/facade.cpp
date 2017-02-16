/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "facade.h"

#include <QDir>
#include <QFileInfo>

#include "query.h"

MaildirResourceMailFacade::MaildirResourceMailFacade(const Sink::ResourceContext &context)
    : Sink::GenericFacade<Sink::ApplicationDomain::Mail>(context)
{
    mResultTransformation = [](Sink::ApplicationDomain::ApplicationDomainType &value) {
        Sink::Log::Context ctx{"maildirfacade"};
        if (value.hasProperty(Sink::ApplicationDomain::Mail::MimeMessage::name)) {
            auto mail = Sink::ApplicationDomain::Mail{value};
            const auto mimeMessage = mail.getMimeMessagePath();
            //Transform the mime message property into the actual path on disk.
            auto parts = mimeMessage.split('/');
            auto key = parts.takeLast();
            const auto folderPath = parts.join('/');
            const auto path =  folderPath + "/cur/";

            SinkTraceCtx(ctx) << "Looking for mail in: " << path << key;
            QDir dir(path);
            const QFileInfoList list = dir.entryInfoList(QStringList() << (key+"*"), QDir::Files);
            if (list.size() != 1) {
                SinkErrorCtx(ctx) << "Failed to find message. Directory: " << path << "Key: " << key << "Number of matching files: " << list.size();
                mail.setProperty(Sink::ApplicationDomain::Mail::MimeMessage::name, QVariant());
            } else {
                mail.setMimeMessagePath(list.at(0).filePath());
            }
        }
        value.setChangedProperties(QSet<QByteArray>());
    };
}

MaildirResourceMailFacade::~MaildirResourceMailFacade()
{
}


MaildirResourceFolderFacade::MaildirResourceFolderFacade(const Sink::ResourceContext &context)
    : Sink::GenericFacade<Sink::ApplicationDomain::Folder>(context)
{
}

MaildirResourceFolderFacade::~MaildirResourceFolderFacade()
{
}
