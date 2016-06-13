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

#include "domainadaptor.h"
#include "queryrunner.h"

MaildirResourceMailFacade::MaildirResourceMailFacade(const QByteArray &instanceIdentifier)
    : Sink::GenericFacade<Sink::ApplicationDomain::Mail>(instanceIdentifier, QSharedPointer<MaildirMailAdaptorFactory>::create())
{
    mResultTransformation = [](Sink::ApplicationDomain::ApplicationDomainType &value) {
        if (value.hasProperty("mimeMessage")) {
            const auto property = value.getProperty("mimeMessage");
            //Transform the mime message property into the actual path on disk.
            const auto mimeMessage = property.toString();
            auto parts = mimeMessage.split('/');
            auto key = parts.takeLast();
            const auto folderPath = parts.join('/');
            const auto path =  folderPath + "/cur/";

            Trace() << "Looking for mail in: " << path << key;
            QDir dir(path);
            const QFileInfoList list = dir.entryInfoList(QStringList() << (key+"*"), QDir::Files);
            if (list.size() != 1) {
                Warning() << "Failed to find message " << path << key << list.size();
                value.setProperty("mimeMessage", QVariant());
            } else {
                value.setProperty("mimeMessage", list.at(0).filePath());
            }
        }
        value.setChangedProperties(QSet<QByteArray>());
    };
}

MaildirResourceMailFacade::~MaildirResourceMailFacade()
{
}

QPair<KAsync::Job<void>, Sink::ResultEmitter<Sink::ApplicationDomain::Mail::Ptr>::Ptr> MaildirResourceMailFacade::load(const Sink::Query &query)
{
    return Sink::GenericFacade<Sink::ApplicationDomain::Mail>::load(query);
}


MaildirResourceFolderFacade::MaildirResourceFolderFacade(const QByteArray &instanceIdentifier)
    : Sink::GenericFacade<Sink::ApplicationDomain::Folder>(instanceIdentifier, QSharedPointer<MaildirFolderAdaptorFactory>::create())
{
}

MaildirResourceFolderFacade::~MaildirResourceFolderFacade()
{
}
