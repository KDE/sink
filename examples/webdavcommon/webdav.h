/*
 *   Copyright (C) 2018 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#pragma once

#include "synchronizer.h"

#include <KDAV2/DavCollection>
#include <KDAV2/DavItem>
#include <KDAV2/DavUrl>

class WebDavSynchronizer : public Sink::Synchronizer
{
public:
    WebDavSynchronizer(const Sink::ResourceContext &, KDAV2::Protocol, const QByteArray &collectionName, const QByteArrayList &itemNames);

    QList<Synchronizer::SyncRequest> getSyncRequests(const Sink::QueryBase &query) Q_DECL_OVERRIDE;
    KAsync::Job<void> synchronizeWithSource(const Sink::QueryBase &query) Q_DECL_OVERRIDE;

protected:
    KAsync::Job<QByteArray> createItem(const QByteArray &vcard, const QByteArray &contentType, const QByteArray &uid, const QByteArray &collectionRid);
    KAsync::Job<QByteArray> modifyItem(const QByteArray &oldRemoteId, const QByteArray &vcard, const QByteArray &contentType, const QByteArray &collectionRid);
    KAsync::Job<QByteArray> removeItem(const QByteArray &oldRemoteId);

    KAsync::Job<void> createCollection(const QByteArray &collectionRid);
    KAsync::Job<void> removeCollection(const QByteArray &collectionRid);
    KAsync::Job<void> modifyCollection(const QByteArray &collectionRid);

    /**
     * Called with the list of discovered collections. It's purpose should be
     * adding the said collections to the store.
     */
    virtual void updateLocalCollections(KDAV2::DavCollection::List collections) = 0;

    /**
     * Called when discovering a new item, or when an item has been modified.
     * It's purpose should be adding the said item to the store.
     *
     * `collectionLocalId` is the local collection id of the item.
     */
    virtual void updateLocalItem(const KDAV2::DavItem &item, const QByteArray &collectionLocalId) = 0;

    KAsync::Job<void> synchronizeCollection(const KDAV2::DavUrl &collectionUrl, const QByteArray &collectionRid, const QByteArray &collectionLocalId, const QByteArray &ctag);


    static QByteArray resourceID(const KDAV2::DavCollection &);
    static QByteArray resourceID(const KDAV2::DavItem &);

    /**
     * Used to get the url of an item / collection with the given remote ID
     */
    KDAV2::DavUrl urlOf(const KDAV2::DavUrl &serverUrl, const QByteArray &remoteId);

    /**
     * Used to get the url of an item / collection with the given remote ID,
     * and append `itemPath` to the path of the URI.
     *
     * Useful when adding a new item to a collection
     */
    KDAV2::DavUrl urlOf(const KDAV2::DavUrl &serverUrl, const QByteArray &collectionRemoteId, const QString &itemPath);

private:
    KAsync::Job<KDAV2::DavUrl> discoverServer();

    KDAV2::Protocol mProtocol;
    const QByteArray mCollectionType;
    const QByteArrayList mEntityTypes;

    KDAV2::DavUrl mCachedServer;
    QUrl mServer;
    QString mUsername;
};
