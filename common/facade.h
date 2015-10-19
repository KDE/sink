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

#pragma once

#include "facadeinterface.h"

#include <QByteArray>
#include <Async/Async>

#include "resourceaccess.h"
#include "commands.h"
#include "domainadaptor.h"
#include "log.h"
#include "resultset.h"
#include "entitystorage.h"

/**
 * A QueryRunner runs a query and updates the corresponding result set.
 * 
 * The lifetime of the QueryRunner is defined by the resut set (otherwise it's doing useless work),
 * and by how long a result set must be updated. If the query is one off the runner dies after the execution,
 * otherwise it lives on the react to changes and updates the corresponding result set.
 * 
 * QueryRunner has to keep ResourceAccess alive in order to keep getting updates.
 */
class QueryRunner : public QObject
{
    Q_OBJECT
public:
    typedef std::function<KAsync::Job<qint64>(qint64 oldRevision)> QueryFunction;

    QueryRunner(const Akonadi2::Query &query) : mLatestRevision(0) {};
    /**
     * Starts query
     */
    KAsync::Job<void> run(qint64 newRevision = 0)
    {
        //TODO: JOBAPI: that last empty .then should not be necessary
        //TODO: remove newRevision
        return queryFunction(mLatestRevision + 1).then<void, qint64>([this](qint64 revision) {
            mLatestRevision = revision;
        }).then<void>([](){});
    }

    /**
     * Set the query to run
     */
    void setQuery(const QueryFunction &query)
    {
        queryFunction = query;
    }

public slots:
    /**
     * Rerun query with new revision
     */
    void revisionChanged(qint64 newRevision)
    {
        Trace() << "New revision: " << newRevision;
        run(newRevision).exec();
    }

private:
    QueryFunction queryFunction;
    qint64 mLatestRevision;
};

static ResultSet fullScan(const Akonadi2::Storage::Transaction &transaction, const QByteArray &bufferType)
{
    //TODO use a result set with an iterator, to read values on demand
    QVector<QByteArray> keys;
    transaction.openDatabase(bufferType + ".main").scan(QByteArray(), [&](const QByteArray &key, const QByteArray &value) -> bool {
        //Skip internals
        if (Akonadi2::Storage::isInternalKey(key)) {
            return true;
        }
        keys << Akonadi2::Storage::uidFromKey(key);
        return true;
    },
    [](const Akonadi2::Storage::Error &error) {
        qWarning() << "Error during query: " << error.message;
    });

    Trace() << "Full scan found " << keys.size() << " results";
    return ResultSet(keys);
}


namespace Akonadi2 {
/**
 * Default facade implementation for resources that are implemented in a separate process using the ResourceAccess class.
 * 
 * Ideally a basic resource has no implementation effort for the facades and can simply instanciate default implementations (meaning it only has to implement the factory with all supported types).
 * A resource has to implement:
 * * A facade factory registering all available facades
 * * An adaptor factory if it uses special resource buffers (default implementation can be used otherwise)
 * * A mapping between resource and buffertype if default can't be used.
 *
 * Additionally a resource only has to provide a synchronizer plugin to execute the synchronization
 */
template <typename DomainType>
class GenericFacade: public Akonadi2::StoreFacade<DomainType>
{
public:
    /**
     * Create a new GenericFacade
     * 
     * @param resourceIdentifier is the identifier of the resource instance
     * @param adaptorFactory is the adaptor factory used to generate the mappings from domain to resource types and vice versa
     */
    GenericFacade(const QByteArray &resourceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory = DomainTypeAdaptorFactoryInterface::Ptr(), const QSharedPointer<EntityStorage<DomainType> > storage = QSharedPointer<EntityStorage<DomainType> >(), const QSharedPointer<Akonadi2::ResourceAccessInterface> resourceAccess = QSharedPointer<Akonadi2::ResourceAccessInterface>())
        : Akonadi2::StoreFacade<DomainType>(),
        mResourceAccess(resourceAccess),
        mStorage(storage),
        mDomainTypeAdaptorFactory(adaptorFactory),
        mResourceInstanceIdentifier(resourceIdentifier)
    {
        if (!mResourceAccess) {
            mResourceAccess = QSharedPointer<Akonadi2::ResourceAccess>::create(resourceIdentifier);
        }
        if (!mStorage) {
            mStorage = QSharedPointer<EntityStorage<DomainType> >::create(resourceIdentifier);
            const auto bufferType = bufferTypeForDomainType();

            mStorage->readEntity = [bufferType, this] (const Akonadi2::Storage::Transaction &transaction, const QByteArray &key, const std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &, Akonadi2::Operation)> &resultCallback)
            {
                //This only works for a 1:1 mapping of resource to domain types.
                //Not i.e. for tags that are stored as flags in each entity of an imap store.
                //additional properties that don't have a 1:1 mapping (such as separately stored tags),
                //could be added to the adaptor.
                transaction.openDatabase(bufferType + ".main").findLatest(key, [=](const QByteArray &key, const QByteArray &value) -> bool {
                    Akonadi2::EntityBuffer buffer(value.data(), value.size());
                    const Akonadi2::Entity &entity = buffer.entity();
                    const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(entity.metadata());
                    Q_ASSERT(metadataBuffer);
                    const qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
                    resultCallback(DomainType::Ptr::create(mResourceInstanceIdentifier, Akonadi2::Storage::uidFromKey(key), revision, mDomainTypeAdaptorFactory->createAdaptor(entity)), metadataBuffer->operation());
                    return false;
                },
                [](const Akonadi2::Storage::Error &error) {
                    qWarning() << "Error during query: " << error.message;
                });
            };

            mStorage->loadInitialResultSet = [bufferType, this] (const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters) -> ResultSet
            {
                QSet<QByteArray> appliedFilters;
                auto resultSet = Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::queryIndexes(query, mResourceInstanceIdentifier, appliedFilters, transaction);
                remainingFilters = query.propertyFilter.keys().toSet() - appliedFilters;

                //We do a full scan if there were no indexes available to create the initial set.
                if (appliedFilters.isEmpty()) {
                    //TODO this should be replaced by an index lookup as well
                    return fullScan(transaction, bufferType);
                }
                return resultSet;
            };

            mStorage->loadIncrementalResultSet = [bufferType, this] (qint64 baseRevision, const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters) -> ResultSet
            {
                auto revisionCounter = QSharedPointer<qint64>::create(baseRevision);
                return ResultSet([bufferType, revisionCounter, &transaction, this]() -> QByteArray {
                    const qint64 topRevision = Akonadi2::Storage::maxRevision(transaction);
                    //Spit out the revision keys one by one.
                    while (*revisionCounter <= topRevision) {
                        const auto uid = Akonadi2::Storage::getUidFromRevision(transaction, *revisionCounter);
                        const auto type = Akonadi2::Storage::getTypeFromRevision(transaction, *revisionCounter);
                        Trace() << "Revision" << *revisionCounter << type << uid;
                        if (type != bufferType) {
                            //Skip revision
                            *revisionCounter += 1;
                            continue;
                        }
                        const auto key = Akonadi2::Storage::assembleKey(uid, *revisionCounter);
                        *revisionCounter += 1;
                        return key;
                    }
                    //We're done
                    return QByteArray();
                });
            };
        }
    }

    ~GenericFacade()
    {
    }

    static QByteArray bufferTypeForDomainType()
    {
        //We happen to have a one to one mapping
        return Akonadi2::ApplicationDomain::getTypeName<DomainType>();
    }

    KAsync::Job<void> create(const DomainType &domainObject) Q_DECL_OVERRIDE
    {
        if (!mDomainTypeAdaptorFactory) {
            Warning() << "No domain type adaptor factory available";
            return KAsync::error<void>();
        }
        flatbuffers::FlatBufferBuilder entityFbb;
        mDomainTypeAdaptorFactory->createBuffer(domainObject, entityFbb);
        return mResourceAccess->sendCreateCommand(bufferTypeForDomainType(), QByteArray::fromRawData(reinterpret_cast<const char*>(entityFbb.GetBufferPointer()), entityFbb.GetSize()));
    }

    KAsync::Job<void> modify(const DomainType &domainObject) Q_DECL_OVERRIDE
    {
        if (!mDomainTypeAdaptorFactory) {
            Warning() << "No domain type adaptor factory available";
            return KAsync::error<void>();
        }
        flatbuffers::FlatBufferBuilder entityFbb;
        mDomainTypeAdaptorFactory->createBuffer(domainObject, entityFbb);
        return mResourceAccess->sendModifyCommand(domainObject.identifier(), domainObject.revision(), bufferTypeForDomainType(), QByteArrayList(), QByteArray::fromRawData(reinterpret_cast<const char*>(entityFbb.GetBufferPointer()), entityFbb.GetSize()));
    }

    KAsync::Job<void> remove(const DomainType &domainObject) Q_DECL_OVERRIDE
    {
        return mResourceAccess->sendDeleteCommand(domainObject.identifier(), domainObject.revision(), bufferTypeForDomainType());
    }

    //TODO JOBAPI return job from sync continuation to execute it as subjob?
    KAsync::Job<void> load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > &resultProvider) Q_DECL_OVERRIDE
    {
        auto runner = QSharedPointer<QueryRunner>::create(query);
        QWeakPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > weakResultProvider = resultProvider;
        runner->setQuery([this, weakResultProvider, query] (qint64 oldRevision) -> KAsync::Job<qint64> {
            return KAsync::start<qint64>([this, weakResultProvider, query, oldRevision](KAsync::Future<qint64> &future) {
                Trace() << "Executing query " << oldRevision;
                auto resultProvider = weakResultProvider.toStrongRef();
                if (!resultProvider) {
                    Warning() << "Tried executing query after result provider is already gone";
                    future.setError(0, QString());
                    future.setFinished();
                    return;
                }
                load(query, resultProvider, oldRevision).template then<void, qint64>([&future, this](qint64 queriedRevision) {
                    //TODO set revision in result provider?
                    //TODO update all existing results with new revision
                    mResourceAccess->sendRevisionReplayedCommand(queriedRevision);
                    future.setValue(queriedRevision);
                    future.setFinished();
                }).exec();
            });
        });

        //In case of a live query we keep the runner for as long alive as the result provider exists
        if (query.liveQuery) {
            resultProvider->setQueryRunner(runner);
            //Ensure the connection is open, if it wasn't already opened
            //TODO If we are not connected already, we have to check for the latest revision once connected, otherwise we could miss some updates
            mResourceAccess->open();
            QObject::connect(mResourceAccess.data(), &Akonadi2::ResourceAccess::revisionChanged, runner.data(), &QueryRunner::revisionChanged);
        }

        //We have to capture the runner to keep it alive
        return synchronizeResource(query.syncOnDemand, query.processAll).template then<void>([runner](KAsync::Future<void> &future) {
            runner->run().then<void>([&future]() {
                future.setFinished();
            }).exec();
        });
    }

protected:
    KAsync::Job<void> synchronizeResource(bool sync, bool processAll)
    {
        //TODO check if a sync is necessary
        //TODO Only sync what was requested
        //TODO timeout
        if (sync || processAll) {
            return KAsync::start<void>([=](KAsync::Future<void> &future) {
                mResourceAccess->synchronizeResource(sync, processAll).then<void>([&future]() {
                    future.setFinished();
                }).exec();
            });
        }
        return KAsync::null<void>();
    }

private:
    virtual KAsync::Job<qint64> load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > &resultProvider, qint64 oldRevision)
    {
        return KAsync::start<qint64>([=]() -> qint64 {
            return mStorage->read(query, oldRevision, resultProvider);
        });
    }

protected:
    //TODO use one resource access instance per application & per resource
    QSharedPointer<Akonadi2::ResourceAccessInterface> mResourceAccess;
    QSharedPointer<EntityStorage<DomainType> > mStorage;
    DomainTypeAdaptorFactoryInterface::Ptr mDomainTypeAdaptorFactory;
    QByteArray mResourceInstanceIdentifier;
};

}
