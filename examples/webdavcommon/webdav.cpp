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

#include <KDAV2/DavCollectionsFetchJob>
#include <KDAV2/DavItemFetchJob>
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

    auto collectionsFetchJob = new KDAV2::DavCollectionsFetchJob{serverUrl()};
    auto job = runJob(collectionsFetchJob).then([this, collectionsFetchJob](const KAsync::Error &error) {
        if (error) {
            SinkWarning() << "Failed to synchronize collections:" << error;
        } else {
            updateLocalCollections(collectionsFetchJob->collections());
        }

        return collectionsFetchJob->collections();
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

        // Same but for items.
        // Quirk: may contain a collection Id (see below)
        auto itemsResourceIDs = QSharedPointer<QSet<QByteArray>>::create();

        return job
            .serialEach([=](const KDAV2::DavCollection &collection) {
                auto collectionResourceID = resourceID(collection);

                collectionResourceIDs->insert(collectionResourceID);

                if (unchanged(collection)) {
                    SinkTrace() << "Collection unchanged:" << collectionResourceID;

                    // It seems that doing this prevent the items in the
                    // collection to be removed when doing scanForRemovals
                    // below (since the collection is unchanged, we do not go
                    // through all of its items).
                    // Behaviour copied from the previous code.
                    itemsResourceIDs->insert(collectionResourceID);

                    return KAsync::null<void>();
                }

                SinkTrace() << "Syncing collection:" << collectionResourceID;
                return synchronizeCollection(collection, progress, total, itemsResourceIDs);
            })
            .then([=]() {
                scanForRemovals(collectionName, [&collectionResourceIDs](const QByteArray &remoteId) {
                    return collectionResourceIDs->contains(remoteId);
                });
                scanForRemovals(itemName, [&itemsResourceIDs](const QByteArray &remoteId) {
                    return itemsResourceIDs->contains(remoteId);
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

    // The ETag cache is useless here, since `sinkStore()` IS the cache.
    auto cache = std::make_shared<KDAV2::EtagCache>();
    auto davItemsListJob = new KDAV2::DavItemsListJob(collection.url(), std::move(cache));

    return runJob(davItemsListJob)
        .then([this, davItemsListJob, total] {
            auto items = davItemsListJob->items();
            *total += items.size();
            return KAsync::value(items);
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
    return runJob(itemFetchJob)
        .then([this, itemFetchJob(std::move(itemFetchJob)), collectionLocalRid] {
            auto item = itemFetchJob->item();
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

QByteArray WebDavSynchronizer::resourceID(const KDAV2::DavCollection &collection)
{
    return collection.url().toDisplayString().toUtf8();
}

QByteArray WebDavSynchronizer::resourceID(const KDAV2::DavItem &item)
{
    return item.url().toDisplayString().toUtf8();
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
