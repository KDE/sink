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
#include "resourceaccess.h"
#include "resultprovider.h"
#include "domaintypeadaptorfactoryinterface.h"
#include "storage.h"
#include "query.h"

/**
 * Base clase because you can't have the Q_OBJECT macro in template classes
 */
class QueryRunnerBase : public QObject
{
    Q_OBJECT
public:
    typedef std::function<void(Sink::ApplicationDomain::ApplicationDomainType &domainObject)> ResultTransformation;

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

/**
 * A QueryRunner runs a query and updates the corresponding result set.
 * 
 * The lifetime of the QueryRunner is defined by the resut set (otherwise it's doing useless work),
 * and by how long a result set must be updated. If the query is one off the runner dies after the execution,
 * otherwise it lives on the react to changes and updates the corresponding result set.
 * 
 * QueryRunner has to keep ResourceAccess alive in order to keep getting updates.
 */
template<typename DomainType>
class QueryRunner : public QueryRunnerBase
{
public:
    QueryRunner(const Sink::Query &query, const Sink::ResourceAccessInterface::Ptr &, const QByteArray &instanceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &, const QByteArray &bufferType);
    virtual ~QueryRunner();

    /**
     * Allows you to run a transformation on every result.
     * This transformation is executed in the query thread.
     */
    void setResultTransformation(const ResultTransformation &transformation);

    typename Sink::ResultEmitter<typename DomainType::Ptr>::Ptr emitter();

private:
    QSharedPointer<Sink::ResourceAccessInterface> mResourceAccess;
    QSharedPointer<Sink::ResultProvider<typename DomainType::Ptr> > mResultProvider;
    ResultTransformation mResultTransformation;
    int mOffset;
    int mBatchSize;
};

