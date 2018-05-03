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
#include <KDAV2/DavItemCreateJob>
#include <KDAV2/DavItemDeleteJob>
#include <KDAV2/DavItemFetchJob>
#include <KDAV2/DavItemModifyJob>
#include <KDAV2/DavItemsListJob>
#include <KDAV2/EtagCache>

#include <QNetworkReply>

static int translateDavError(KJob *job)
{
    using Sink::ApplicationDomain::ErrorCode;

    const int responseCode = dynamic_cast<KDAV2::DavJobBase *>(job)->latestResponseCode();

    switch (responseCode) {
        case QNetworkReply::HostNotFoundError:
            return ErrorCode::NoServerError;
        // Since we don't login we will just not have the necessary permissions ot view the object
        case QNetworkReply::OperationCanceledError:
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

WebDavSynchronizer::WebDavSynchronizer(const Sink::ResourceContext &context,
    KDAV2::Protocol protocol, QByteArray collectionName, QByteArray itemName)
    : Sink::Synchronizer(context),
      protocol(protocol),
      collectionName(std::move(collectionName)),
      itemName(std::move(itemName))
{
    auto config = ResourceConfig::getConfiguration(context.instanceId());

    server = QUrl::fromUserInput(config.value("server").toString());
    username = config.value("username").toString();
}

QList<Sink::Synchronizer::SyncRequest> WebDavSynchronizer::getSyncRequests(const Sink::QueryBase &query)
{
    QList<Synchronizer::SyncRequest> list;
    if (!query.type().isEmpty()) {
        // We want to synchronize something specific
        list << Synchronizer::SyncRequest{ query };
    } else {
        // We want to synchronize everything

        // Item synchronization does the collections anyway
        // list << Synchronizer::SyncRequest{ Sink::QueryBase(collectionName) };
        list << Synchronizer::SyncRequest{ Sink::QueryBase(itemName) };
    }
    return list;
}

KAsync::Job<void> WebDavSynchronizer::synchronizeWithSource(const Sink::QueryBase &query)
{
    if (query.type() != collectionName && query.type() != itemName) {
        return KAsync::null<void>();
    }

    SinkLog() << "Synchronizing" << query.type() << "through WebDAV at:" << serverUrl().url();

    auto collectionsFetchJob = new KDAV2::DavCollectionsFetchJob{ serverUrl() };
    auto job = runJob<KDAV2::DavCollection::List>(collectionsFetchJob,
        [](KJob *job) { return static_cast<KDAV2::DavCollectionsFetchJob *>(job)->collections(); })
                   .then([this](const KDAV2::DavCollection::List &collections) {
                       updateLocalCollections(collections);
                       return collections;
                   });

    if (query.type() == collectionName) {
        // Do nothing more
        return job;
    } else if (query.type() == itemName) {
        auto progress = QSharedPointer<int>::create(0);
        auto total = QSharedPointer<int>::create(0);

        // Will contain the resource Id of all collections to be able to scan
        // for collections to be removed.
        auto collectionResourceIDs = QSharedPointer<QSet<QByteArray>>::create();

        return job
            .serialEach([=](const KDAV2::DavCollection &collection) {
                auto collectionResourceID = resourceID(collection);

                collectionResourceIDs->insert(collectionResourceID);

                if (unchanged(collection)) {
                    SinkTrace() << "Collection unchanged:" << collectionResourceID;

                    return KAsync::null<void>();
                }

                SinkTrace() << "Syncing collection:" << collectionResourceID;
                auto itemsResourceIDs = QSharedPointer<QSet<QByteArray>>::create();
                return synchronizeCollection(collection, progress, total, itemsResourceIDs)
                .then([=] {
                    scanForRemovals(itemName, [&itemsResourceIDs](const QByteArray &remoteId) {
                        return itemsResourceIDs->contains(remoteId);
                    });
                });
            })
            .then([=]() {
                scanForRemovals(collectionName, [&collectionResourceIDs](const QByteArray &remoteId) {
                    return collectionResourceIDs->contains(remoteId);
                });
            });
    } else {
        SinkWarning() << "Unknown query type";
        return KAsync::null<void>();
    }
}

KAsync::Job<void> WebDavSynchronizer::synchronizeCollection(const KDAV2::DavCollection &collection,
    QSharedPointer<int> progress, QSharedPointer<int> total,
    QSharedPointer<QSet<QByteArray>> itemsResourceIDs)
{
    auto collectionRid = resourceID(collection);
    auto ctag = collection.CTag().toLatin1();

    auto localRid = collectionLocalResourceID(collection);

    auto cache = std::make_shared<KDAV2::EtagCache>();
    auto davItemsListJob = new KDAV2::DavItemsListJob(collection.url(), std::move(cache));

    return runJob<KDAV2::DavItem::List>(davItemsListJob,
        [](KJob *job) { return static_cast<KDAV2::DavItemsListJob *>(job)->items(); })
        .then([this, total](const KDAV2::DavItem::List &items) {
            *total += items.size();
            return items;
        })
        .serialEach([this, collectionRid, localRid, progress(std::move(progress)), total(std::move(total)),
                        itemsResourceIDs(std::move(itemsResourceIDs))](const KDAV2::DavItem &item) {
            auto itemRid = resourceID(item);

            itemsResourceIDs->insert(itemRid);

            if (unchanged(item)) {
                SinkTrace() << "Item unchanged:" << itemRid;
                return KAsync::null<void>();
            }

            SinkTrace() << "Syncing item:" << itemRid;
            return synchronizeItem(item, localRid, progress, total);
        })
        .then([this, collectionRid, ctag] {
            // Update the local CTag to be able to tell if the collection is unchanged
            syncStore().writeValue(collectionRid + "_ctag", ctag);
        });
}

KAsync::Job<void> WebDavSynchronizer::synchronizeItem(const KDAV2::DavItem &item,
    const QByteArray &collectionLocalRid, QSharedPointer<int> progress, QSharedPointer<int> total)
{
    auto etag = item.etag().toLatin1();

    auto itemFetchJob = new KDAV2::DavItemFetchJob(item);
    return runJob<KDAV2::DavItem>(
        itemFetchJob, [](KJob *job) { return static_cast<KDAV2::DavItemFetchJob *>(job)->item(); })
        .then([this, collectionLocalRid](const KDAV2::DavItem &item) {
            updateLocalItem(item, collectionLocalRid);
            return item;
        })
        .then([this, etag, progress(std::move(progress)), total(std::move(total))](const KDAV2::DavItem &item) {
            // Update the local ETag to be able to tell if the item is unchanged
            syncStore().writeValue(resourceID(item) + "_etag", etag);

            *progress += 1;
            reportProgress(*progress, *total);
            if ((*progress % 5) == 0) {
                commit();
            }
        });
}

KAsync::Job<void> WebDavSynchronizer::createItem(const KDAV2::DavItem &item)
{
    auto job = new KDAV2::DavItemCreateJob(item);
    return runJob(job).then([] { SinkTrace() << "Done creating item"; });
}

KAsync::Job<void> WebDavSynchronizer::removeItem(const KDAV2::DavItem &item)
{
    auto job = new KDAV2::DavItemDeleteJob(item);
    return runJob(job).then([] { SinkTrace() << "Done removing item"; });
}

KAsync::Job<void> WebDavSynchronizer::modifyItem(const KDAV2::DavItem &item)
{
    auto job = new KDAV2::DavItemModifyJob(item);
    return runJob(job).then([] { SinkTrace() << "Done modifying item"; });
}

// There is no "DavCollectionCreateJob"
/*
KAsync::Job<void> WebDavSynchronizer::createCollection(const KDAV2::DavCollection &collection)
{
    auto job = new KDAV2::DavCollectionCreateJob(collection);
    return runJob(job);
}
*/

KAsync::Job<void> WebDavSynchronizer::removeCollection(const KDAV2::DavUrl &url)
{
    auto job = new KDAV2::DavCollectionDeleteJob(url);
    return runJob(job).then([] { SinkLog() << "Done removing collection"; });
}

// Useless without using the `setProperty` method of DavCollectionModifyJob
/*
KAsync::Job<void> WebDavSynchronizer::modifyCollection(const KDAV2::DavUrl &url)
{
    auto job = new KDAV2::DavCollectionModifyJob(url);
    return runJob(job).then([] { SinkLog() << "Done modifying collection"; });
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

KDAV2::DavUrl WebDavSynchronizer::urlOf(const QByteArray &remoteId)
{
    auto davurl = serverUrl();
    auto url = davurl.url();
    url.setPath(remoteId);
    SinkLog() << "Returning URL:" << url.toEncoded();
    davurl.setUrl(url);
    return davurl;
}

KDAV2::DavUrl WebDavSynchronizer::urlOf(const QByteArray &collectionRemoteId, const QString &itemPath)
{
    return urlOf(collectionRemoteId + "/" + itemPath.toUtf8());
}

bool WebDavSynchronizer::unchanged(const KDAV2::DavCollection &collection)
{
    auto ctag = collection.CTag().toLatin1();
    return ctag == syncStore().readValue(resourceID(collection) + "_ctag");
}

bool WebDavSynchronizer::unchanged(const KDAV2::DavItem &item)
{
    auto etag = item.etag().toLatin1();
    return etag == syncStore().readValue(resourceID(item) + "_etag");
}

KDAV2::DavUrl WebDavSynchronizer::serverUrl() const
{
    if (secret().isEmpty()) {
        return {};
    }

    auto result = server;
    result.setUserName(username);
    result.setPassword(secret());

    return KDAV2::DavUrl{ result, protocol };
}
