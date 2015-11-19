/*
 *   Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include "resultset.h"
#include "domainadaptor.h"

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
    GenericFacade(const QByteArray &resourceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory = DomainTypeAdaptorFactoryInterface::Ptr(), const QSharedPointer<Akonadi2::ResourceAccessInterface> resourceAccess = QSharedPointer<Akonadi2::ResourceAccessInterface>());
    ~GenericFacade();

    static QByteArray bufferTypeForDomainType();
    KAsync::Job<void> create(const DomainType &domainObject) Q_DECL_OVERRIDE;
    KAsync::Job<void> modify(const DomainType &domainObject) Q_DECL_OVERRIDE;
    KAsync::Job<void> remove(const DomainType &domainObject) Q_DECL_OVERRIDE;
    KAsync::Job<void> load(const Akonadi2::Query &query, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider) Q_DECL_OVERRIDE;

private:
    //TODO move into result provider?
    static void replaySet(ResultSet &resultSet, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);

    void readEntity(const Akonadi2::Storage::Transaction &transaction, const QByteArray &key, const std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &, Akonadi2::Operation)> &resultCallback);

    ResultSet loadInitialResultSet(const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters);
    ResultSet loadIncrementalResultSet(qint64 baseRevision, const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters);

    ResultSet filterSet(const ResultSet &resultSet, const std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter, const Akonadi2::Storage::Transaction &transaction, bool initialQuery);
    std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> getFilter(const QSet<QByteArray> remainingFilters, const Akonadi2::Query &query);
    qint64 load(const Akonadi2::Query &query, const std::function<ResultSet(Akonadi2::Storage::Transaction &, QSet<QByteArray> &)> &baseSetRetriever, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);
    qint64 executeIncrementalQuery(const Akonadi2::Query &query, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);
    qint64 executeInitialQuery(const Akonadi2::Query &query, const typename DomainType::Ptr &parent, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);

protected:
    //TODO use one resource access instance per application & per resource
    QSharedPointer<Akonadi2::ResourceAccessInterface> mResourceAccess;
    DomainTypeAdaptorFactoryInterface::Ptr mDomainTypeAdaptorFactory;
    QByteArray mResourceInstanceIdentifier;
};

}
