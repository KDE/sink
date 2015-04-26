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

#include "clientapi.h"

#include <QByteArray>

#include "async/src/async.h"
#include "resourceaccess.h"
#include "commands.h"
#include "createentity_generated.h"
#include "domainadaptor.h"
#include "entitybuffer.h"
#include "log.h"

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
    typedef std::function<Async::Job<qint64>(qint64 oldRevision, qint64 newRevision)> QueryFunction;

    QueryRunner(const Akonadi2::Query &query) : mLatestRevision(0) {};
    /**
     * Starts query
     */
    Async::Job<void> run(qint64 newRevision = 0)
    {
        //TODO: JOBAPI: that last empty .then should not be necessary
        return queryFunction(mLatestRevision, newRevision).then<void, qint64>([this](qint64 revision) {
            mLatestRevision = revision;
        }).then<void>([](){});
    }

    /**
     *
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
        run(newRevision).exec();
    }

private:
    QueryFunction queryFunction;
    qint64 mLatestRevision;
};

namespace Akonadi2 {
    class ResourceAccess;
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
    GenericFacade(const QByteArray &resourceIdentifier, const QSharedPointer<DomainTypeAdaptorFactoryInterface<DomainType> > &adaptorFactory = QSharedPointer<DomainTypeAdaptorFactoryInterface<DomainType> >())
        : Akonadi2::StoreFacade<DomainType>(),
        mResourceAccess(new ResourceAccess(resourceIdentifier)),
        mDomainTypeAdaptorFactory(adaptorFactory)
    {
    }

    ~GenericFacade()
    {
    }

    static QByteArray bufferTypeForDomainType()
    {
        //We happen to have a one to one mapping
        return Akonadi2::ApplicationDomain::getTypeName<DomainType>();
    }

    Async::Job<void> create(const Akonadi2::ApplicationDomain::Event &domainObject) Q_DECL_OVERRIDE
    {
        if (!mDomainTypeAdaptorFactory) {
            Warning() << "No domain type adaptor factory available";
        }
        flatbuffers::FlatBufferBuilder entityFbb;
        mDomainTypeAdaptorFactory->createBuffer(domainObject, entityFbb);
        return sendCreateCommand(bufferTypeForDomainType(), QByteArray::fromRawData(reinterpret_cast<const char*>(entityFbb.GetBufferPointer()), entityFbb.GetSize()));
    }

    Async::Job<void> modify(const Akonadi2::ApplicationDomain::Event &domainObject) Q_DECL_OVERRIDE
    {
        //TODO
        return Async::null<void>();
    }

    Async::Job<void> remove(const Akonadi2::ApplicationDomain::Event &domainObject) Q_DECL_OVERRIDE
    {
        //TODO
        return Async::null<void>();
    }

    //TODO JOBAPI return job from sync continuation to execute it as subjob?
    Async::Job<void> load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > &resultProvider) Q_DECL_OVERRIDE
    {
        auto runner = QSharedPointer<QueryRunner>::create(query);
        QWeakPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > weakResultProvider = resultProvider;
        runner->setQuery([this, weakResultProvider, query] (qint64 oldRevision, qint64 newRevision) -> Async::Job<qint64> {
            return Async::start<qint64>([this, weakResultProvider, query](Async::Future<qint64> &future) {
                auto resultProvider = weakResultProvider.toStrongRef();
                if (!resultProvider) {
                    Warning() << "Tried executing query after result provider is already gone";
                    future.setError(0, QString());
                    future.setFinished();
                    return;
                }
                //TODO only emit changes and don't replace everything
                resultProvider->clear();
                //rerun query
                auto addCallback = std::bind(&Akonadi2::ResultProvider<typename DomainType::Ptr>::add, resultProvider, std::placeholders::_1);
                load(query, addCallback).template then<void, qint64>([resultProvider, &future](qint64 queriedRevision) {
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
            QObject::connect(mResourceAccess.data(), &Akonadi2::ResourceAccess::revisionChanged, runner.data(), &QueryRunner::revisionChanged);
        }

        //We have to capture the runner to keep it alive
        return synchronizeResource(query.syncOnDemand, query.processAll).template then<void>([runner](Async::Future<void> &future) {
            runner->run().then<void>([&future]() {
                future.setFinished();
            }).exec();
        });
    }

protected:
    Async::Job<void> sendCreateCommand(const QByteArray &resourceBufferType, const QByteArray &buffer)
    {
        flatbuffers::FlatBufferBuilder fbb;
        //This is the resource buffer type and not the domain type
        auto type = fbb.CreateString(resourceBufferType.constData());
        auto delta = Akonadi2::EntityBuffer::appendAsVector(fbb, buffer.constData(), buffer.size());
        auto location = Akonadi2::Commands::CreateCreateEntity(fbb, type, delta);
        Akonadi2::Commands::FinishCreateEntityBuffer(fbb, location);
        mResourceAccess->open();
        return mResourceAccess->sendCommand(Akonadi2::Commands::CreateEntityCommand, fbb);
    }

    Async::Job<void> synchronizeResource(bool sync, bool processAll)
    {
        //TODO check if a sync is necessary
        //TODO Only sync what was requested
        //TODO timeout
        //TODO the synchronization should normally not be necessary: We just return what is already available.

        if (sync || processAll) {
            return Async::start<void>([=](Async::Future<void> &future) {
                mResourceAccess->open();
                mResourceAccess->synchronizeResource(sync, processAll).then<void>([&future]() {
                    future.setFinished();
                }).exec();
            });
        }
        return Async::null<void>();
    }

    virtual Async::Job<qint64> load(const Akonadi2::Query &query, const std::function<void(const typename DomainType::Ptr &)> &resultCallback) { return Async::null<qint64>(); };

protected:
    //TODO use one resource access instance per application => make static
    QSharedPointer<Akonadi2::ResourceAccess> mResourceAccess;
    QSharedPointer<DomainTypeAdaptorFactoryInterface<DomainType> > mDomainTypeAdaptorFactory;
};

}
