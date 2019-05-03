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

#include "webdav.h"

#include "applicationdomaintype.h"
#include "resourceconfig.h"

#include <KDAV2/DavCollectionDeleteJob>
#include <KDAV2/DavCollectionModifyJob>
#include <KDAV2/DavCollectionsFetchJob>
#include <KDAV2/DavDiscoveryJob>
#include <KDAV2/DavItemCreateJob>
#include <KDAV2/DavItemDeleteJob>
#include <KDAV2/DavItemsFetchJob>
#include <KDAV2/DavItemModifyJob>
#include <KDAV2/DavItemsListJob>

#include <QNetworkReply>

static int translateDavError(KJob *job)
{
    using Sink::ApplicationDomain::ErrorCode;

    const int responseCode = static_cast<KDAV2::DavJobBase *>(job)->latestResponseCode();
    SinkWarning() << "Response code: " << responseCode;

    switch (responseCode) {
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::ContentNotFoundError: //If we can't find the content we probably messed up the url configuration
            return ErrorCode::NoServerError;
        case QNetworkReply::AuthenticationRequiredError:
        case QNetworkReply::InternalServerError: //The kolab server reports a HTTP 500 instead of 401 on invalid credentials (we could introspect the error message for the 401 error code)
        case QNetworkReply::OperationCanceledError: // Since we don't login we will just not have the necessary permissions ot view the object
            return ErrorCode::LoginError;
    }
    return ErrorCode::UnknownError;
}

static KAsync::Job<void> runJob(KJob *job)
{
    return KAsync::start<void>([job](KAsync::Future<void> &future) {
        QObject::connect(job, &KJob::result, [&future](KJob *job) {
            SinkTrace() << "Job done: " << job->metaObject()->className();
            if (job->error()) {
                SinkWarning() << "Job failed: " << job->errorString() << job->metaObject()->className() << job->error();
                auto proxyError = translateDavError(job);
                future.setError(proxyError, job->errorString());
            } else {
                future.setFinished();
            }
        });
        SinkTrace() << "Starting job: " << job->metaObject()->className();
        job->start();
    });
}

template <typename T>
static KAsync::Job<T> runJob(KJob *job, const std::function<T(KJob *)> &func)
{
    return KAsync::start<T>([job, func](KAsync::Future<T> &future) {
        QObject::connect(job, &KJob::result, [&future, func](KJob *job) {
            SinkTrace() << "Job done: " << job->metaObject()->className();
            if (job->error()) {
                SinkWarning() << "Job failed: " << job->errorString() << job->metaObject()->className() << job->error();
                auto proxyError = translateDavError(job);
                future.setError(proxyError, job->errorString());
            } else {
                future.setValue(func(job));
                future.setFinished();
            }
        });
        SinkTrace() << "Starting job: " << job->metaObject()->className();
        job->start();
    });
}

WebDavSynchronizer::WebDavSynchronizer(const Sink::ResourceContext &context, KDAV2::Protocol protocol, const QByteArray &collectionType, const QByteArrayList &entityTypes)
    : Sink::Synchronizer(context),
      mProtocol(protocol),
      mCollectionType(collectionType),
      mEntityTypes(entityTypes)
{
    auto config = ResourceConfig::getConfiguration(context.instanceId());

    mServer = QUrl::fromUserInput(config.value("server").toString());
    mUsername = config.value("username").toString();
}

QList<Sink::Synchronizer::SyncRequest> WebDavSynchronizer::getSyncRequests(const Sink::QueryBase &query)
{
    QList<Synchronizer::SyncRequest> list;
    if (!query.type().isEmpty()) {
        // We want to synchronize something specific
        list << Synchronizer::SyncRequest{query};
    } else {
        // We want to synchronize everything
        list << Synchronizer::SyncRequest{Sink::QueryBase(mCollectionType)};
        //This request depends on the previous one so we flush first.
        for (const auto &type : mEntityTypes) {
            //TODO one flush would be enough
            list << Synchronizer::SyncRequest{Sink::QueryBase{type}, QByteArray{}, Synchronizer::SyncRequest::RequestFlush};
        }
    }
    return list;
}

KAsync::Job<void> WebDavSynchronizer::synchronizeWithSource(const Sink::QueryBase &query)
{
    return discoverServer().then([this, query] (const KDAV2::DavUrl &serverUrl) {
        SinkLogCtx(mLogCtx) << "Synchronizing" << query.type() << "through WebDAV at:" << serverUrl.url();
        if (query.type() == mCollectionType) {
            return runJob<KDAV2::DavCollection::List>(new KDAV2::DavCollectionsFetchJob{ serverUrl },
                [](KJob *job) { return static_cast<KDAV2::DavCollectionsFetchJob *>(job)->collections(); })
                .then([this](const KDAV2::DavCollection::List &collections) {

                    QSet<QByteArray> collectionRemoteIDs;
                    for (const auto &collection : collections) {
                        collectionRemoteIDs.insert(resourceID(collection));
                    }
                    scanForRemovals(mCollectionType, [&](const QByteArray &remoteId) {
                        return collectionRemoteIDs.contains(remoteId);
                    });
                    updateLocalCollections(collections);
                });
        } else if (mEntityTypes.contains(query.type())) {
            const QSet<QByteArray> collectionsToSync = [&] {
                if (query.hasFilter(mCollectionType)) {
                    auto folderFilter = query.getFilter(mCollectionType);
                    auto localIds = resolveFilter(folderFilter);
                    return localIds.toSet();
                } else {
                    //Find all enabled collections
                    Sink::Query query;
                    query.setType(mCollectionType);
                    query.filter("enabled", {true});
                    return resolveQuery(query).toSet();
                }
            }();
            if (collectionsToSync.isEmpty()) {
                SinkTraceCtx(mLogCtx) << "No collections to sync:" << query;
                return KAsync::null();
            }
            SinkTraceCtx(mLogCtx) << "Synchronizing collections: " << collectionsToSync;

            return runJob<KDAV2::DavCollection::List>(new KDAV2::DavCollectionsFetchJob{ serverUrl },
                [](KJob *job) { return static_cast<KDAV2::DavCollectionsFetchJob *>(job)->collections(); })
                .serialEach([=](const KDAV2::DavCollection &collection) {
                    const auto collectionRid = resourceID(collection);
                    const auto localId = syncStore().resolveRemoteId(mCollectionType, collectionRid);
                    //Filter list of folders to sync
                    if (!collectionsToSync.contains(localId)) {
                        return KAsync::null();
                    }
                    return synchronizeCollection(collection.url(), collectionRid, localId, collection.CTag().toLatin1())
                        .then([=] (const KAsync::Error &error) {
                            if (error) {
                                SinkWarningCtx(mLogCtx) << "Failed to synchronized folder" << error;
                            }
                            //Ignore synchronization errors for individual collections, the next one might work.
                            return KAsync::null();
                        });
                });
        } else {
            SinkWarning() << "Unknown query type" << query;
            return KAsync::null<void>();
        }

    });

}

KAsync::Job<void> WebDavSynchronizer::synchronizeCollection(const KDAV2::DavUrl &collectionUrl, const QByteArray &collectionRid, const QByteArray &collectionLocalId, const QByteArray &ctag)
{
    auto progress = QSharedPointer<int>::create(0);
    auto total = QSharedPointer<int>::create(0);
    if (ctag == syncStore().readValue(collectionRid + "_ctag")) {
        SinkTraceCtx(mLogCtx) << "Collection unchanged:" << collectionRid;
        return KAsync::null<void>();
    }
    SinkLogCtx(mLogCtx) << "Syncing collection:" << collectionRid << ctag << collectionUrl;

    auto itemsResourceIDs = QSharedPointer<QSet<QByteArray>>::create();

    auto listJob = new KDAV2::DavItemsListJob(collectionUrl);
    if (mCollectionType == "calendar") {
        listJob->setContentMimeTypes({{"VEVENT"}, {"VTODO"}});
    }
    return runJob<KDAV2::DavItem::List>(listJob,
        [](KJob *job) { return static_cast<KDAV2::DavItemsListJob *>(job)->items(); })
        .then([=](const KDAV2::DavItem::List &items) {
            SinkLogCtx(mLogCtx) << "Found" << items.size() << "items on the server";
            QStringList itemsToFetch;
            for (const auto &item : items) {
                const auto itemRid = resourceID(item);
                itemsResourceIDs->insert(itemRid);
                if (item.etag().toLatin1() == syncStore().readValue(collectionRid, itemRid + "_etag")) {
                    SinkTraceCtx(mLogCtx) << "Item unchanged:" << itemRid;
                    continue;
                }
                itemsToFetch << item.url().url().toDisplayString();
            }
            if (itemsToFetch.isEmpty()) {
                return KAsync::null();
            }
            *total += itemsToFetch.size();
            return runJob<KDAV2::DavItem::List>(new KDAV2::DavItemsFetchJob(collectionUrl, itemsToFetch),
                [](KJob *job) { return static_cast<KDAV2::DavItemsFetchJob *>(job)->items(); })
                .then([=] (const KDAV2::DavItem::List &items) {
                    for (const auto &item : items) {
                        updateLocalItem(item, collectionLocalId);
                        syncStore().writeValue(collectionRid, resourceID(item) + "_etag", item.etag().toLatin1());
                    }

                });
        })
        .then([=] {
            // Update the local CTag to be able to tell if the collection is unchanged
            syncStore().writeValue(collectionRid + "_ctag", ctag);

            for (const auto &entityType : mEntityTypes) {
                scanForRemovals(entityType,
                    [&](const std::function<void(const QByteArray &)> &callback) {
                        //FIXME: The collection type just happens to have the same name as the parent collection property
                        const auto collectionProperty = mCollectionType;
                        store().indexLookup(entityType, collectionProperty, collectionLocalId, callback);
                    },
                    [&itemsResourceIDs](const QByteArray &remoteId) {
                        return itemsResourceIDs->contains(remoteId);
                    });
            }
        });
}

KAsync::Job<KDAV2::DavUrl> WebDavSynchronizer::discoverServer()
{
    if (mCachedServer.url().isValid()) {
        return KAsync::value(mCachedServer);
    }
    if (!mServer.isValid()) {
        return KAsync::error<KDAV2::DavUrl>(Sink::ApplicationDomain::ConfigurationError, "Invalid server url: " + mServer.toString());
    }

    if (secret().isEmpty()) {
        return KAsync::error<KDAV2::DavUrl>(Sink::ApplicationDomain::ConfigurationError, "No secret");
    }

    auto result = mServer;
    result.setUserName(mUsername);
    result.setPassword(secret());
    const KDAV2::DavUrl serverUrl{result, mProtocol};

    return runJob<KDAV2::DavUrl>(new KDAV2::DavDiscoveryJob(serverUrl, mCollectionType == "addressbook" ? "carddav" : "caldav"), [=] (KJob *job) {
        auto url = serverUrl;
        url.setUrl(static_cast<KDAV2::DavDiscoveryJob*>(job)->url());
        mCachedServer = url;
        return url;
    });
}

KAsync::Job<QByteArray> WebDavSynchronizer::createItem(const QByteArray &vcard, const QByteArray &contentType, const QByteArray &rid, const QByteArray &collectionRid)
{
    return discoverServer()
        .then([=] (const KDAV2::DavUrl &serverUrl) {
            KDAV2::DavItem remoteItem;
            remoteItem.setData(vcard);
            remoteItem.setContentType(contentType);
            remoteItem.setUrl(urlOf(serverUrl, collectionRid, rid));
            SinkLogCtx(mLogCtx) << "Creating:" <<  "Rid: " <<  rid << "Content-Type: " << contentType << "Url: " << remoteItem.url().url() << "Content:\n" << vcard;

            return runJob<KDAV2::DavItem>(new KDAV2::DavItemCreateJob(remoteItem), [](KJob *job) { return static_cast<KDAV2::DavItemCreateJob*>(job)->item(); })
                .then([=] (const KDAV2::DavItem &remoteItem) {
                    syncStore().writeValue(collectionRid, resourceID(remoteItem) + "_etag", remoteItem.etag().toLatin1());
                    return resourceID(remoteItem);
                });
        });

}

KAsync::Job<QByteArray> WebDavSynchronizer::modifyItem(const QByteArray &oldRemoteId, const QByteArray &vcard, const QByteArray &contentType, const QByteArray &collectionRid)
{
    return discoverServer()
        .then([=] (const KDAV2::DavUrl &serverUrl) {
            KDAV2::DavItem remoteItem;
            remoteItem.setData(vcard);
            remoteItem.setContentType(contentType);
            remoteItem.setUrl(urlOf(serverUrl, oldRemoteId));
            remoteItem.setEtag(syncStore().readValue(collectionRid, oldRemoteId + "_etag"));
            SinkLogCtx(mLogCtx) << "Modifying:" << "Content-Type: " << contentType << "Url: " << remoteItem.url().url() << "Etag: " << remoteItem.etag() << "Content:\n" << vcard;

            return runJob<KDAV2::DavItem>(new KDAV2::DavItemModifyJob(remoteItem), [](KJob *job) { return static_cast<KDAV2::DavItemModifyJob*>(job)->item(); })
                .then([=] (const KDAV2::DavItem &remoteItem) {
                    const auto remoteId = resourceID(remoteItem);
                    //Should never change if not moved
                    Q_ASSERT(remoteId == oldRemoteId);
                    syncStore().writeValue(collectionRid, remoteId + "_etag", remoteItem.etag().toLatin1());
                    return remoteId;
                });
        });
}

KAsync::Job<QByteArray> WebDavSynchronizer::removeItem(const QByteArray &oldRemoteId)
{
    return discoverServer()
        .then([=] (const KDAV2::DavUrl &serverUrl) {
            SinkLogCtx(mLogCtx) << "Removing:" << oldRemoteId;
            // We only need the URL in the DAV item for removal
            KDAV2::DavItem remoteItem;
            remoteItem.setUrl(urlOf(serverUrl, oldRemoteId));
            return runJob(new KDAV2::DavItemDeleteJob(remoteItem))
                .then([] {
                    return QByteArray{};
                });
        });
}

// There is no "DavCollectionCreateJob"
/*
KAsync::Job<void> WebDavSynchronizer::createCollection(const QByteArray &collectionRid)
{
    auto job = new KDAV2::DavCollectionCreateJob(collection);
    return runJob(job);
}
*/

KAsync::Job<void> WebDavSynchronizer::removeCollection(const QByteArray &collectionRid)
{
    return discoverServer()
        .then([=] (const KDAV2::DavUrl &serverUrl) {
            return runJob(new KDAV2::DavCollectionDeleteJob(urlOf(serverUrl, collectionRid))).then([this] { SinkLogCtx(mLogCtx) << "Done removing collection"; });
        });
}

// Useless without using the `setProperty` method of DavCollectionModifyJob
/*
KAsync::Job<void> WebDavSynchronizer::modifyCollection(const QByteArray &collectionRid)
{
    auto job = new KDAV2::DavCollectionModifyJob(url);
    return runJob(job).then([] { SinkLogCtx(mLogCtx) << "Done modifying collection"; });
}
*/

QByteArray WebDavSynchronizer::resourceID(const KDAV2::DavCollection &collection)
{
    return collection.url().url().path().toUtf8();
}

QByteArray WebDavSynchronizer::resourceID(const KDAV2::DavItem &item)
{
    return item.url().url().path().toUtf8();
}

KDAV2::DavUrl WebDavSynchronizer::urlOf(const KDAV2::DavUrl &serverUrl, const QByteArray &remoteId)
{
    auto davurl = serverUrl;
    auto url = davurl.url();
    url.setPath(remoteId);
    davurl.setUrl(url);
    return davurl;
}

KDAV2::DavUrl WebDavSynchronizer::urlOf(const KDAV2::DavUrl &serverUrl, const QByteArray &collectionRemoteId, const QString &itemPath)
{
    return urlOf(serverUrl, collectionRemoteId + itemPath.toUtf8());
}
