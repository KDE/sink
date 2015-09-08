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

#include "domainadaptor.h"

DummyResourceFacade::DummyResourceFacade(const QByteArray &instanceIdentifier)
    : Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>(instanceIdentifier, QSharedPointer<DummyEventAdaptorFactory>::create())
{
}

DummyResourceFacade::~DummyResourceFacade()
{
}

DummyResourceMailFacade::DummyResourceMailFacade(const QByteArray &instanceIdentifier)
    : Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Mail>(instanceIdentifier, QSharedPointer<DummyMailAdaptorFactory>::create())
{
}

DummyResourceMailFacade::~DummyResourceMailFacade()
{
}

static void addFolder(const QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Folder::Ptr> > &resultProvider, QByteArray uid, QString name, QString icon)
{
    auto folder = Akonadi2::ApplicationDomain::Folder::Ptr::create();
    folder->setProperty("name", name);
    folder->setProperty("uid", uid);
    resultProvider->add(folder);
}

KAsync::Job<void> load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Folder::Ptr> > &resultProvider)
{
    //Dummy implementation for a fixed set of folders
    addFolder(resultProvider, "inbox", "INBOX", "mail-folder-inbox");
    addFolder(resultProvider, "sent", "Sent", "mail-folder-sent");
    addFolder(resultProvider, "trash", "Trash", "user-trash");
    addFolder(resultProvider, "drafts", "Drafts", "document-edit");
    addFolder(resultProvider, "1", "dragons", "folder");
    addFolder(resultProvider, "1", "super mega long tailed dragons", "folder");
    resultProvider->initialResultSetComplete();
    resultProvider->complete();
    return KAsync::null<void>();
}

