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
    typedef std::function<KAsync::Job<qint64>(qint64 oldRevision, qint64 newRevision)> QueryFunction;

    QueryRunner(const Akonadi2::Query &query) : mLatestRevision(0) {};
    /**
     * Starts query
     */
    KAsync::Job<void> run(qint64 newRevision = 0)
    {
        //TODO: JOBAPI: that last empty .then should not be necessary
        if (mLatestRevision == newRevision && mLatestRevision > 0) {
            return KAsync::null<void>();
        }
        return queryFunction(mLatestRevision + 1, newRevision).then<void, qint64>([this](qint64 revision) {
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
        mStorage(storage ? storage : QSharedPointer<EntityStorage<DomainType> >::create(resourceIdentifier, adaptorFactory)),
        mDomainTypeAdaptorFactory(adaptorFactory),
        mResourceInstanceIdentifier(resourceIdentifier)
    {
        if (!mResourceAccess) {
            mResourceAccess = QSharedPointer<Akonadi2::ResourceAccess>::create(resourceIdentifier);
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
        runner->setQuery([this, weakResultProvider, query] (qint64 oldRevision, qint64 newRevision) -> KAsync::Job<qint64> {
            return KAsync::start<qint64>([this, weakResultProvider, query, oldRevision, newRevision](KAsync::Future<qint64> &future) {
                Trace() << "Executing query " << oldRevision << newRevision;
                auto resultProvider = weakResultProvider.toStrongRef();
                if (!resultProvider) {
                    Warning() << "Tried executing query after result provider is already gone";
                    future.setError(0, QString());
                    future.setFinished();
                    return;
                }
                load(query, resultProvider, oldRevision, newRevision).template then<void, qint64>([&future](qint64 queriedRevision) {
                    //TODO set revision in result provider?
                    //TODO update all existing results with new revision
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
    virtual KAsync::Job<qint64> load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > &resultProvider, qint64 oldRevision, qint64 newRevision)
    {
        return KAsync::start<qint64>([=]() -> qint64 {
            mStorage->read(query, qMakePair(oldRevision, newRevision), resultProvider);
            return newRevision;
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
