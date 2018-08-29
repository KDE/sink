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
#include <KDAV2/DavItemsFetchJob>
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
    KDAV2::Protocol protocol, QByteArray mCollectionType, QByteArray mEntityType)
    : Sink::Synchronizer(context),
      protocol(protocol),
      mCollectionType(std::move(mCollectionType)),
      mEntityType(std::move(mEntityType))
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
        list << Synchronizer::SyncRequest{ query };
    } else {
        // We want to synchronize everything

        // Item synchronization does the collections anyway
        // list << Synchronizer::SyncRequest{ Sink::QueryBase(mCollectionType) };
        list << Synchronizer::SyncRequest{ Sink::QueryBase(mEntityType) };
    }
    return list;
}

KAsync::Job<void> WebDavSynchronizer::synchronizeWithSource(const Sink::QueryBase &query)
{
    if (query.type() != mCollectionType && query.type() != mEntityType) {
        SinkWarning() << "Received synchronization reuqest with unkown type" << query;
        return KAsync::null<void>();
    }

    SinkLog() << "Synchronizing" << query.type() << "through WebDAV at:" << serverUrl().url();

    auto collectionsFetchJob = new KDAV2::DavCollectionsFetchJob{ serverUrl() };
    auto job = runJob<KDAV2::DavCollection::List>(collectionsFetchJob,
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

            return collections;
        });

    if (query.type() == mCollectionType) {
        // Do nothing more
        return job;
    } else if (query.type() == mEntityType) {
        return job.serialEach([=](const KDAV2::DavCollection &collection) {
                return synchronizeCollection(collection);
            });
    } else {
        SinkWarning() << "Unknown query type";
        return KAsync::null<void>();
    }
}

KAsync::Job<void> WebDavSynchronizer::synchronizeCollection(const KDAV2::DavCollection &collection)
{
    auto progress = QSharedPointer<int>::create(0);
    auto total = QSharedPointer<int>::create(0);
    auto collectionRid = resourceID(collection);
    auto ctag = collection.CTag().toLatin1();
    if (ctag == syncStore().readValue(collectionRid + "_ctag")) {
        SinkTrace() << "Collection unchanged:" << collectionRid;
        return KAsync::null<void>();
    }
    SinkLog() << "Syncing collection:" << collectionRid << collection.displayName() << ctag;

    auto collectionLocalId = collectionLocalResourceID(collection);

    auto itemsResourceIDs = QSharedPointer<QSet<QByteArray>>::create();
    auto cache = std::make_shared<KDAV2::EtagCache>();
    auto listJob = new KDAV2::DavItemsListJob(collection.url(), std::move(cache));
    listJob->setContentMimeTypes({{"VEVENT"}, {"VTODO"}});
    return runJob<KDAV2::DavItem::List>(listJob,
        [](KJob *job) { return static_cast<KDAV2::DavItemsListJob *>(job)->items(); })
        .then([=](const KDAV2::DavItem::List &items) {
            SinkLog() << "Found" << items.size() << "items on the server";
            if (items.isEmpty()) {
                return KAsync::null();
            }
            *total += items.size();
            QStringList itemsUrls;
            for (const auto &item : items) {
                itemsUrls << item.url().url().toDisplayString();
            }
            return runJob<KDAV2::DavItem::List>(new KDAV2::DavItemsFetchJob(collection.url(), itemsUrls),
                [](KJob *job) { return static_cast<KDAV2::DavItemsFetchJob *>(job)->items(); })
                .then([=] (const KDAV2::DavItem::List &items) {
                    for (const auto &item : items) {
                        const auto itemRid = resourceID(item);
                        itemsResourceIDs->insert(itemRid);
                        if (item.etag().toLatin1() == syncStore().readValue(itemRid + "_etag")) {
                            SinkTrace() << "Item unchanged:" << itemRid;
                        } else {
                            updateLocalItem(item, collectionLocalId);
                            // Update the local ETag to be able to tell if the item is unchanged
                            syncStore().writeValue(itemRid + "_etag", item.etag().toLatin1());
                        }
                    }

                });
        })
        .then([=] {
            // Update the local CTag to be able to tell if the collection is unchanged
            syncStore().writeValue(collectionRid + "_ctag", ctag);

            scanForRemovals(mEntityType,
                [&](const std::function<void(const QByteArray &)> &callback) {
                    //FIXME: The collection type just happens to have the same name as the parent collection property
                    const auto collectionProperty = mCollectionType;
                    store().indexLookup(mEntityType, collectionProperty, collectionLocalId, callback);
                },
                [&itemsResourceIDs](const QByteArray &remoteId) {
                    return itemsResourceIDs->contains(remoteId);
                });
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
    davurl.setUrl(url);
    return davurl;
}

KDAV2::DavUrl WebDavSynchronizer::urlOf(const QByteArray &collectionRemoteId, const QString &itemPath)
{
    return urlOf(collectionRemoteId + itemPath.toUtf8());
}

KDAV2::DavUrl WebDavSynchronizer::serverUrl() const
{
    if (secret().isEmpty()) {
        return {};
    }

    auto result = mServer;
    result.setUserName(mUsername);
    result.setPassword(secret());

    return KDAV2::DavUrl{ result, protocol };
}
