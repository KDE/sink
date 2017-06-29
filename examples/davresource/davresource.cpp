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

#include "davresource.h"

#include "facade.h"
#include "resourceconfig.h"
#include "log.h"
#include "definitions.h"
#include "synchronizer.h"
#include "inspector.h"

#include "facadefactory.h"
#include "adaptorfactoryregistry.h"

#include "contactpreprocessor.h"

#include <KDAV2/DavCollection>
#include <KDAV2/DavCollectionsFetchJob>
#include <KDAV2/DavItem>
#include <KDAV2/DavItemsListJob>
#include <KDAV2/DavItemFetchJob>
#include <KDAV2/EtagCache>

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_CONTACT "contact"
#define ENTITY_TYPE_ADDRESSBOOK "addressbook"

using namespace Sink;

static KAsync::Job<void> runJob(KJob *job)
{
    return KAsync::start<void>([job](KAsync::Future<void> &future) {
        QObject::connect(job, &KJob::result, [&future](KJob *job) {
            SinkTrace() << "Job done: " << job->metaObject()->className();
            if (job->error()) {
                SinkWarning() << "Job failed: " << job->errorString();
                future.setError(job->error(), job->errorString());
            } else {
                future.setFinished();
            }
        });
        SinkTrace() << "Starting job: " << job->metaObject()->className();
        job->start();
    });
}

class ContactSynchronizer : public Sink::Synchronizer {
public:
    ContactSynchronizer(const Sink::ResourceContext &resourceContext)
        : Sink::Synchronizer(resourceContext)
    {

    }

    QByteArray createAddressbook(const QString &addressbookName, const QString &addressbookPath, const QString &parentAddressbookRid)
    {
        SinkTrace() << "Creating addressbook: " << addressbookName << parentAddressbookRid;
        const auto remoteId = addressbookPath.toUtf8();
        const auto bufferType = ENTITY_TYPE_ADDRESSBOOK;
        Sink::ApplicationDomain::Addressbook addressbook;
        addressbook.setName(addressbookName);
        QHash<QByteArray, Query::Comparator> mergeCriteria;

        if (!parentAddressbookRid.isEmpty()) {
            addressbook.setParent(syncStore().resolveRemoteId(ENTITY_TYPE_ADDRESSBOOK, parentAddressbookRid.toUtf8()));
        }
        createOrModify(bufferType, remoteId, addressbook, mergeCriteria);
        return remoteId;
    }

    void synchronizeAddressbooks(const KDAV2::DavCollection::List &addressbookList)
    {
        const QByteArray bufferType = ENTITY_TYPE_ADDRESSBOOK;
        SinkTrace() << "Found addressbooks " << addressbookList.size();

        QVector<QByteArray> ridList;
        for(const auto &f : addressbookList) {
            const auto &rid = getRid(f);
            SinkTrace() << "Found addressbook:" << rid;
            ridList.append(rid);
            createAddressbook(f.displayName(), rid, "");
        }

        scanForRemovals(bufferType,
            [&ridList](const QByteArray &remoteId) -> bool {
                return ridList.contains(remoteId);
            }
        );
    }

    QList<Synchronizer::SyncRequest> getSyncRequests(const Sink::QueryBase &query) Q_DECL_OVERRIDE
    {
        QList<Synchronizer::SyncRequest> list;
        if (!query.type().isEmpty()) {
            //We want to synchronize something specific
            list << Synchronizer::SyncRequest{query};
        } else {
            //We want to synchronize everything
            list << Synchronizer::SyncRequest{Sink::QueryBase(ApplicationDomain::getTypeName<ApplicationDomain::Addressbook>())};
            list << Synchronizer::SyncRequest{Sink::QueryBase(ApplicationDomain::getTypeName<ApplicationDomain::Contact>())};
        }
        return list;
    }

    static QByteArray getRid(const KDAV2::DavItem &item)
    {
        return item.url().toDisplayString().toUtf8();
    }

    static QByteArray getRid(const KDAV2::DavCollection &item)
    {
        return item.url().toDisplayString().toUtf8();
    }

    KAsync::Job<void> synchronizeWithSource(const Sink::QueryBase &query) Q_DECL_OVERRIDE
    {
        if (query.type() == ApplicationDomain::getTypeName<ApplicationDomain::Addressbook>()) {
            SinkLogCtx(mLogCtx) << "Synchronizing addressbooks:" <<  mResourceUrl.url();
            auto collectionsFetchJob = new KDAV2::DavCollectionsFetchJob(mResourceUrl);
            auto job = runJob(collectionsFetchJob).then([this, collectionsFetchJob] (const KAsync::Error &error) {
                if (error) {
                    SinkWarningCtx(mLogCtx) << "Failed to synchronize addressbooks." << collectionsFetchJob->errorString();
                } else {
                    synchronizeAddressbooks(collectionsFetchJob ->collections());
                }
            });
            return job;
        } else if (query.type() == ApplicationDomain::getTypeName<ApplicationDomain::Contact>()) {
            SinkLogCtx(mLogCtx) << "Synchronizing contacts.";
            auto ridList = QSharedPointer<QByteArrayList>::create();
            auto collectionsFetchJob = new KDAV2::DavCollectionsFetchJob(mResourceUrl);
            auto job = runJob(collectionsFetchJob).then([this, collectionsFetchJob] {
                synchronizeAddressbooks(collectionsFetchJob ->collections());
                return collectionsFetchJob->collections();
            })
            .serialEach([this, ridList](const KDAV2::DavCollection &collection) {
                auto collId = getRid(collection);
                const auto addressbookLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_ADDRESSBOOK, collId);
                auto ctag = collection.CTag().toLatin1();
                if (ctag != syncStore().readValue(collId + "_ctagXX")) {
                    SinkTraceCtx(mLogCtx) << "Syncing " << collId;
                    auto cache = std::shared_ptr<KDAV2::EtagCache>(new KDAV2::EtagCache());
                    auto davItemsListJob = new KDAV2::DavItemsListJob(collection.url(), cache);
                    const QByteArray bufferType = ENTITY_TYPE_CONTACT;
                    QHash<QByteArray, Query::Comparator> mergeCriteria;
                    auto colljob = runJob(davItemsListJob).then([davItemsListJob] {
                        return KAsync::value(davItemsListJob->items());
                    })
                    .serialEach([=] (const KDAV2::DavItem &item) {
                        QByteArray rid = getRid(item);
                        if (item.etag().toLatin1() != syncStore().readValue(rid + "_etag")){
                            SinkTrace() << "Updating " << rid;
                            auto davItemFetchJob = new KDAV2::DavItemFetchJob(item);
                            auto itemjob = runJob(davItemFetchJob)
                            .then([=] {
                                const auto item = davItemFetchJob->item();
                                const auto rid = getRid(item);
                                Sink::ApplicationDomain::Contact contact;
                                contact.setVcard(item.data());
                                contact.setAddressbook(addressbookLocalId);
                                createOrModify(bufferType, rid, contact, mergeCriteria);
                                return item;
                            })
                            .then([this, ridList] (const KDAV2::DavItem &item) {
                                const auto rid = getRid(item);
                                syncStore().writeValue(rid + "_etag", item.etag().toLatin1());
                                ridList->append(rid);
                                return rid;
                            });
                            return itemjob;
                        } else {
                            ridList->append(rid);
                            return KAsync::value(rid);
                        }
                    })
                    .then([this, collId, ctag] () {
                        syncStore().writeValue(collId + "_ctag", ctag);
                    });
                    return colljob;
                } else {
                    SinkTraceCtx(mLogCtx) << "Collection unchanged: " << ctag;
                    // for(const auto &item : addressbook) {
                    //      ridList->append(rid);
                    // }
                    return KAsync::null<void>();
                }
            })
            .then<void>([this, ridList] () {
                scanForRemovals(ENTITY_TYPE_CONTACT,
                    [&ridList](const QByteArray &remoteId) -> bool {
                        return ridList->contains(remoteId);
                });
            });
            return job;
        } else {
            return KAsync::null<void>();
        }
    }

KAsync::Job<QByteArray> replay(const ApplicationDomain::Contact &contact, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        return KAsync::null<QByteArray>();
    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Addressbook &addressbook, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        return KAsync::null<QByteArray>();
    }

public:
    KDAV2::DavUrl mResourceUrl;
};


DavResource::DavResource(const Sink::ResourceContext &resourceContext)
    : Sink::GenericResource(resourceContext)
{
    auto config = ResourceConfig::getConfiguration(resourceContext.instanceId());
    auto resourceUrl = QUrl::fromUserInput(config.value("server").toString());
    resourceUrl.setUserName(config.value("username").toString());
    resourceUrl.setPassword(config.value("password").toString());

    mResourceUrl = KDAV2::DavUrl(resourceUrl, KDAV2::CardDav);

    auto synchronizer = QSharedPointer<ContactSynchronizer>::create(resourceContext);
    synchronizer->mResourceUrl = mResourceUrl;
    setupSynchronizer(synchronizer);

    setupPreprocessors(ENTITY_TYPE_CONTACT, QVector<Sink::Preprocessor*>() << new ContactPropertyExtractor);
}


DavResourceFactory::DavResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent,
            {Sink::ApplicationDomain::ResourceCapabilities::Contact::contact,
            Sink::ApplicationDomain::ResourceCapabilities::Contact::addressbook,
            }
            )
{
}

Sink::Resource *DavResourceFactory::createResource(const ResourceContext &context)
{
    return new DavResource(context);
}

void DavResourceFactory::registerFacades(const QByteArray &name, Sink::FacadeFactory &factory)
{
    factory.registerFacade<ApplicationDomain::Contact, DefaultFacade<ApplicationDomain::Contact>>(name);
    factory.registerFacade<ApplicationDomain::Addressbook, DefaultFacade<ApplicationDomain::Addressbook>>(name);
}

void DavResourceFactory::registerAdaptorFactories(const QByteArray &name, Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<ApplicationDomain::Contact, DefaultAdaptorFactory<ApplicationDomain::Contact>>(name);
    registry.registerFactory<ApplicationDomain::Addressbook, DefaultAdaptorFactory<ApplicationDomain::Addressbook>>(name);
}

void DavResourceFactory::removeDataFromDisk(const QByteArray &instanceIdentifier)
{
    DavResource::removeFromDisk(instanceIdentifier);
}
