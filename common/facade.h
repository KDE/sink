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

#include <Async/Async>

#include "resourceaccess.h"
#include "commands.h"
#include "createentity_generated.h"
#include "domainadaptor.h"
#include "entitybuffer.h"
#include "log.h"
#include "storage.h"
#include "resultset.h"

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
    GenericFacade(const QByteArray &resourceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory = DomainTypeAdaptorFactoryInterface::Ptr())
        : Akonadi2::StoreFacade<DomainType>(),
        mResourceAccess(new ResourceAccess(resourceIdentifier)),
        mDomainTypeAdaptorFactory(adaptorFactory),
        mResourceInstanceIdentifier(resourceIdentifier)
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

    KAsync::Job<void> create(const DomainType &domainObject) Q_DECL_OVERRIDE
    {
        if (!mDomainTypeAdaptorFactory) {
            Warning() << "No domain type adaptor factory available";
        }
        flatbuffers::FlatBufferBuilder entityFbb;
        mDomainTypeAdaptorFactory->createBuffer(domainObject, entityFbb);
        return sendCreateCommand(bufferTypeForDomainType(), QByteArray::fromRawData(reinterpret_cast<const char*>(entityFbb.GetBufferPointer()), entityFbb.GetSize()));
    }

    KAsync::Job<void> modify(const DomainType &domainObject) Q_DECL_OVERRIDE
    {
        //TODO
        return KAsync::null<void>();
    }

    KAsync::Job<void> remove(const DomainType &domainObject) Q_DECL_OVERRIDE
    {
        //TODO
        return KAsync::null<void>();
    }

    //TODO JOBAPI return job from sync continuation to execute it as subjob?
    KAsync::Job<void> load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > &resultProvider) Q_DECL_OVERRIDE
    {
        auto runner = QSharedPointer<QueryRunner>::create(query);
        QWeakPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > weakResultProvider = resultProvider;
        runner->setQuery([this, weakResultProvider, query] (qint64 oldRevision, qint64 newRevision) -> KAsync::Job<qint64> {
            return KAsync::start<qint64>([this, weakResultProvider, query, oldRevision, newRevision](KAsync::Future<qint64> &future) {
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
    KAsync::Job<void> sendCreateCommand(const QByteArray &resourceBufferType, const QByteArray &buffer)
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

    KAsync::Job<void> synchronizeResource(bool sync, bool processAll)
    {
        //TODO check if a sync is necessary
        //TODO Only sync what was requested
        //TODO timeout
        //TODO the synchronization should normally not be necessary: We just return what is already available.

        if (sync || processAll) {
            return KAsync::start<void>([=](KAsync::Future<void> &future) {
                mResourceAccess->open();
                mResourceAccess->synchronizeResource(sync, processAll).then<void>([&future]() {
                    future.setFinished();
                }).exec();
            });
        }
        return KAsync::null<void>();
    }

    static void scan(const QSharedPointer<Akonadi2::Storage> &storage, const QByteArray &key, std::function<bool(const QByteArray &key, const Akonadi2::Entity &entity)> callback)
    {
        storage->scan(key, [=](void *keyValue, int keySize, void *dataValue, int dataSize) -> bool {
            //Skip internals
            if (Akonadi2::Storage::isInternalKey(keyValue, keySize)) {
                return true;
            }

            //Extract buffers
            Akonadi2::EntityBuffer buffer(dataValue, dataSize);

            //FIXME implement buffer.isValid()
            // const auto resourceBuffer = Akonadi2::EntityBuffer::readBuffer<DummyEvent>(buffer.entity().resource());
            // const auto localBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::ApplicationDomain::Buffer::Event>(buffer.entity().local());
            // const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(buffer.entity().metadata());

            // if ((!resourceBuffer && !localBuffer) || !metadataBuffer) {
            //     qWarning() << "invalid buffer " << QByteArray::fromRawData(static_cast<char*>(keyValue), keySize);
            //     return true;
            // }
            return callback(QByteArray::fromRawData(static_cast<char*>(keyValue), keySize), buffer.entity());
        },
        [](const Akonadi2::Storage::Error &error) {
            qWarning() << "Error during query: " << error.message;
        });
    }

    static void readValue(const QSharedPointer<Akonadi2::Storage> &storage, const QByteArray &key, const std::function<void(const typename DomainType::Ptr &)> &resultCallback, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory)
    {
        scan(storage, key, [=](const QByteArray &key, const Akonadi2::Entity &entity) {
            const auto metadataBuffer = Akonadi2::EntityBuffer::readBuffer<Akonadi2::Metadata>(entity.metadata());
            qint64 revision = metadataBuffer ? metadataBuffer->revision() : -1;
            //This only works for a 1:1 mapping of resource to domain types.
            //Not i.e. for tags that are stored as flags in each entity of an imap store.
            //additional properties that don't have a 1:1 mapping (such as separately stored tags),
            //could be added to the adaptor
            auto domainObject = QSharedPointer<DomainType>::create("org.kde.dummy.instance1", key, revision, adaptorFactory->createAdaptor(entity));
            resultCallback(domainObject);
            return true;
        });
    }

    static ResultSet fullScan(const QSharedPointer<Akonadi2::Storage> &storage)
    {
        //TODO use a result set with an iterator, to read values on demand
        QVector<QByteArray> keys;
        scan(storage, QByteArray(), [=, &keys](const QByteArray &key, const Akonadi2::Entity &) {
            keys << key;
            return true;
        });
        return ResultSet(keys);
    }

    static ResultSet filteredSet(const ResultSet &resultSet, const std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter, const QSharedPointer<Akonadi2::Storage> &storage, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory)
    {
        auto resultSetPtr = QSharedPointer<ResultSet>::create(resultSet);

        //Read through the source values and return whatever matches the filter
        std::function<bool(std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &)>)> generator = [resultSetPtr, storage, adaptorFactory, filter](std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &)> callback) -> bool {
            while (resultSetPtr->next()) {
                readValue(storage, resultSetPtr->id(), [filter, callback](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject) {
                    if (filter(domainObject)) {
                        callback(domainObject);
                    }
                }, adaptorFactory);
            }
            return false;
        };
        return ResultSet(generator);
    }

    static ResultSet getResultSet(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::Storage> &storage, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory, const QByteArray &resourceInstanceIdentifier)
    {
        QSet<QByteArray> appliedFilters;
        ResultSet resultSet = Akonadi2::ApplicationDomain::TypeImplementation<DomainType>::queryIndexes(query, resourceInstanceIdentifier, appliedFilters);
        const auto remainingFilters = query.propertyFilter.keys().toSet() - appliedFilters;

        //We do a full scan if there were no indexes available to create the initial set.
        if (appliedFilters.isEmpty()) {
            resultSet = fullScan(storage);
        }

        auto filter = [remainingFilters, query](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject) -> bool {
            for (const auto &filterProperty : remainingFilters) {
                //TODO implement other comparison operators than equality
                if (domainObject->getProperty(filterProperty) != query.propertyFilter.value(filterProperty)) {
                    return false;
                }
            }
            return true;
        };

        return filteredSet(resultSet, filter, storage, adaptorFactory);
    }

    virtual KAsync::Job<qint64> load(const Akonadi2::Query &query, const QSharedPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > &resultProvider, qint64 oldRevision, qint64 newRevision)
    {
        return KAsync::start<qint64>([=]() -> qint64 {
            auto storage = QSharedPointer<Akonadi2::Storage>::create(Akonadi2::Store::storageLocation(), mResourceInstanceIdentifier);
            storage->setDefaultErrorHandler([](const Akonadi2::Storage::Error &error) {
                Warning() << "Error during query: " << error.store << error.message;
            });

            storage->startTransaction(Akonadi2::Storage::ReadOnly);
            //TODO start transaction on indexes as well
            const qint64 revision = storage->maxRevision();

            auto resultSet = getResultSet(query, storage, mDomainTypeAdaptorFactory, mResourceInstanceIdentifier);

            // TODO only emit changes and don't replace everything
            resultProvider->clear();
            auto resultCallback = std::bind(&Akonadi2::ResultProvider<typename DomainType::Ptr>::add, resultProvider, std::placeholders::_1);
            while(resultSet.next([resultCallback](const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &value) -> bool {
                resultCallback(Akonadi2::ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<DomainType>(value));
                return true;
            })){};
            storage->abortTransaction();
            return revision;
        });
    }

private:
protected:
    //TODO use one resource access instance per application => make static
    QSharedPointer<Akonadi2::ResourceAccess> mResourceAccess;
    DomainTypeAdaptorFactoryInterface::Ptr mDomainTypeAdaptorFactory;
    QByteArray mResourceInstanceIdentifier;
};

}
