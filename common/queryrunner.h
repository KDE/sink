/*
    Copyright (c) 2015 Christian Mollekopf <mollekopf@kolabsys.com>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#pragma once

#include <QObject>
#include "facadeinterface.h"
#include "resourceaccess.h"
#include "resultprovider.h"
#include "domaintypeadaptorfactoryinterface.h"
#include "storage.h"
#include "query.h"

/**
 * A QueryRunner runs a query and updates the corresponding result set.
 * 
 * The lifetime of the QueryRunner is defined by the resut set (otherwise it's doing useless work),
 * and by how long a result set must be updated. If the query is one off the runner dies after the execution,
 * otherwise it lives on the react to changes and updates the corresponding result set.
 * 
 * QueryRunner has to keep ResourceAccess alive in order to keep getting updates.
 */

class QueryRunnerBase : public QObject
{
    Q_OBJECT
protected:
    typedef std::function<KAsync::Job<void>()> QueryFunction;

    /**
     * Set the query to run
     */
    void setQuery(const QueryFunction &query)
    {
        queryFunction = query;
    }


protected slots:
    /**
     * Rerun query with new revision
     */
    void revisionChanged(qint64 newRevision)
    {
        Trace() << "New revision: " << newRevision;
        run().exec();
    }

private:
    /**
     * Starts query
     */
    KAsync::Job<void> run(qint64 newRevision = 0)
    {
        return queryFunction();
    }

    QueryFunction queryFunction;
};

template<typename DomainType>
class QueryRunner : public QueryRunnerBase
{
public:
    QueryRunner(const Akonadi2::Query &query, const Akonadi2::ResourceAccessInterface::Ptr &, const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &, const QByteArray &bufferType);

    typename Akonadi2::ResultEmitter<typename DomainType::Ptr>::Ptr emitter();

private:
    static void replaySet(ResultSet &resultSet, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);

    void readEntity(const Akonadi2::Storage::NamedDatabase &db, const QByteArray &key, const std::function<void(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &, Akonadi2::Operation)> &resultCallback);

    ResultSet loadInitialResultSet(const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters);
    ResultSet loadIncrementalResultSet(qint64 baseRevision, const Akonadi2::Query &query, Akonadi2::Storage::Transaction &transaction, QSet<QByteArray> &remainingFilters);

    ResultSet filterSet(const ResultSet &resultSet, const std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> &filter, const Akonadi2::Storage::NamedDatabase &db, bool initialQuery);
    std::function<bool(const Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr &domainObject)> getFilter(const QSet<QByteArray> remainingFilters, const Akonadi2::Query &query);
    qint64 load(const Akonadi2::Query &query, const std::function<ResultSet(Akonadi2::Storage::Transaction &, QSet<QByteArray> &)> &baseSetRetriever, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider, bool initialQuery);
    qint64 executeIncrementalQuery(const Akonadi2::Query &query, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);
    qint64 executeInitialQuery(const Akonadi2::Query &query, const typename DomainType::Ptr &parent, Akonadi2::ResultProviderInterface<typename DomainType::Ptr> &resultProvider);

private:
    QSharedPointer<Akonadi2::ResultProvider<typename DomainType::Ptr> > mResultProvider;
    QSharedPointer<Akonadi2::ResourceAccessInterface> mResourceAccess;
    DomainTypeAdaptorFactoryInterface::Ptr mDomainTypeAdaptorFactory;
    QByteArray mResourceInstanceIdentifier;
    QByteArray mBufferType;
    Akonadi2::Query mQuery;
};

