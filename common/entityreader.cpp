/*
 * Copyright (C) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "entityreader.h"

#include "resultset.h"
#include "storage.h"
#include "query.h"
#include "datastorequery.h"

SINK_DEBUG_AREA("entityreader")

using namespace Sink;

QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> EntityReaderUtils::getLatest(const Sink::Storage::NamedDatabase &db, const QByteArray &uid, DomainTypeAdaptorFactoryInterface &adaptorFactory, qint64 &retrievedRevision)
{
    QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> current;
    db.findLatest(uid,
        [&current, &adaptorFactory, &retrievedRevision](const QByteArray &key, const QByteArray &data) -> bool {
            Sink::EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
            if (!buffer.isValid()) {
                SinkWarning() << "Read invalid buffer from disk";
            } else {
                SinkTrace() << "Found value " << key;
                current = adaptorFactory.createAdaptor(buffer.entity());
                retrievedRevision = Sink::Storage::revisionFromKey(key);
            }
            return false;
        },
        [](const Sink::Storage::Error &error) { SinkWarning() << "Failed to read current value from storage: " << error.message; });
    return current;
}

QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> EntityReaderUtils::get(const Sink::Storage::NamedDatabase &db, const QByteArray &key, DomainTypeAdaptorFactoryInterface &adaptorFactory, qint64 &retrievedRevision)
{
    QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> current;
    db.scan(key,
        [&current, &adaptorFactory, &retrievedRevision](const QByteArray &key, const QByteArray &data) -> bool {
            Sink::EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
            if (!buffer.isValid()) {
                SinkWarning() << "Read invalid buffer from disk";
            } else {
                current = adaptorFactory.createAdaptor(buffer.entity());
                retrievedRevision = Sink::Storage::revisionFromKey(key);
            }
            return false;
        },
        [](const Sink::Storage::Error &error) { SinkWarning() << "Failed to read current value from storage: " << error.message; });
    return current;
}

QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> EntityReaderUtils::getPrevious(const Sink::Storage::NamedDatabase &db, const QByteArray &uid, qint64 revision, DomainTypeAdaptorFactoryInterface &adaptorFactory, qint64 &retrievedRevision)
{
    QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> current;
    qint64 latestRevision = 0;
    db.scan(uid,
        [&current, &latestRevision, revision](const QByteArray &key, const QByteArray &) -> bool {
            auto foundRevision = Sink::Storage::revisionFromKey(key);
            if (foundRevision < revision && foundRevision > latestRevision) {
                latestRevision = foundRevision;
            }
            return true;
        },
        [](const Sink::Storage::Error &error) { SinkWarning() << "Failed to read current value from storage: " << error.message; }, true);
    return get(db, Sink::Storage::assembleKey(uid, latestRevision), adaptorFactory, retrievedRevision);
}

template <class DomainType>
EntityReader<DomainType>::EntityReader(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier, Sink::Storage::Transaction &transaction)
    : mResourceInstanceIdentifier(resourceInstanceIdentifier),
    mTransaction(transaction),
    mDomainTypeAdaptorFactoryPtr(Sink::AdaptorFactoryRegistry::instance().getFactory<DomainType>(resourceType)),
    mDomainTypeAdaptorFactory(*mDomainTypeAdaptorFactoryPtr)
{
    Q_ASSERT(!resourceType.isEmpty());
    Q_ASSERT(mDomainTypeAdaptorFactoryPtr);
}

template <class DomainType>
EntityReader<DomainType>::EntityReader(DomainTypeAdaptorFactoryInterface &domainTypeAdaptorFactory, const QByteArray &resourceInstanceIdentifier, Sink::Storage::Transaction &transaction)
    : mResourceInstanceIdentifier(resourceInstanceIdentifier),
    mTransaction(transaction),
    mDomainTypeAdaptorFactory(domainTypeAdaptorFactory)
{

}

template <class DomainType>
DomainType EntityReader<DomainType>::read(const QByteArray &identifier) const
{
    auto typeName = ApplicationDomain::getTypeName<DomainType>();
    auto mainDatabase = Storage::mainDatabase(mTransaction, typeName);
    qint64 retrievedRevision = 0;
    auto bufferAdaptor = EntityReaderUtils::getLatest(mainDatabase, identifier, mDomainTypeAdaptorFactory, retrievedRevision);
    if (!bufferAdaptor) {
        return DomainType();
    }
    return DomainType(mResourceInstanceIdentifier, identifier, retrievedRevision, bufferAdaptor);
}

template <class DomainType>
DomainType EntityReader<DomainType>::readFromKey(const QByteArray &key) const
{
    auto typeName = ApplicationDomain::getTypeName<DomainType>();
    auto mainDatabase = Storage::mainDatabase(mTransaction, typeName);
    qint64 retrievedRevision = 0;
    auto bufferAdaptor = EntityReaderUtils::get(mainDatabase, key, mDomainTypeAdaptorFactory, retrievedRevision);
    const auto identifier = Storage::uidFromKey(key);
    if (!bufferAdaptor) {
        return DomainType();
    }
    return DomainType(mResourceInstanceIdentifier, identifier, retrievedRevision, bufferAdaptor);
}

template <class DomainType>
DomainType EntityReader<DomainType>::readPrevious(const QByteArray &uid, qint64 revision) const
{
    auto typeName = ApplicationDomain::getTypeName<DomainType>();
    auto mainDatabase = Storage::mainDatabase(mTransaction, typeName);
    qint64 retrievedRevision = 0;
    auto bufferAdaptor = EntityReaderUtils::getPrevious(mainDatabase, uid, revision, mDomainTypeAdaptorFactory, retrievedRevision);
    if (!bufferAdaptor) {
        return DomainType();
    }
    return DomainType(mResourceInstanceIdentifier, uid, retrievedRevision, bufferAdaptor);
}

template <class DomainType>
void EntityReader<DomainType>::query(const Sink::Query &query, const std::function<bool(const DomainType &)> &callback)
{
    executeInitialQuery(query, 0, 0,
        [&callback](const typename DomainType::Ptr &value, Sink::Operation operation, const QMap<QByteArray, QVariant> &) -> bool {
            Q_ASSERT(operation == Sink::Operation_Creation);
            return callback(*value);
        });
}

template <class DomainType>
QPair<qint64, qint64> EntityReader<DomainType>::executeInitialQuery(const Sink::Query &query, int offset, int batchsize, const ResultCallback &callback)
{
    QTime time;
    time.start();

    auto preparedQuery = ApplicationDomain::TypeImplementation<DomainType>::prepareQuery(query, mTransaction);
    auto resultSet = preparedQuery->execute();

    SinkTrace() << "Filtered set retrieved. " << Log::TraceTime(time.elapsed());
    auto replayedEntities = replaySet(resultSet, offset, batchsize, callback);

    SinkTrace() << "Initial query took: " << Log::TraceTime(time.elapsed());
    return qMakePair(Sink::Storage::maxRevision(mTransaction), replayedEntities);
}

template <class DomainType>
QPair<qint64, qint64> EntityReader<DomainType>::executeIncrementalQuery(const Sink::Query &query, qint64 lastRevision, const ResultCallback &callback)
{
    QTime time;
    time.start();
    const qint64 baseRevision = lastRevision + 1;

    auto preparedQuery = ApplicationDomain::TypeImplementation<DomainType>::prepareQuery(query, mTransaction);
    auto resultSet = preparedQuery->update(baseRevision);

    SinkTrace() << "Filtered set retrieved. " << Log::TraceTime(time.elapsed());
    auto replayedEntities = replaySet(resultSet, 0, 0, callback);

    SinkTrace() << "Incremental query took: " << Log::TraceTime(time.elapsed());
    return qMakePair(Sink::Storage::maxRevision(mTransaction), replayedEntities);
}

template <class DomainType>
qint64 EntityReader<DomainType>::replaySet(ResultSet &resultSet, int offset, int batchSize, const ResultCallback &callback)
{
    SinkTrace() << "Skipping over " << offset << " results";
    resultSet.skip(offset);
    int counter = 0;
    while (!batchSize || (counter < batchSize)) {
        const bool ret =
            resultSet.next([this, &counter, callback](const ResultSet::Result &result) -> bool {
                counter++;
                auto adaptor = mDomainTypeAdaptorFactory.createAdaptor(result.buffer.entity());
                Q_ASSERT(adaptor);
                return callback(QSharedPointer<DomainType>::create(mResourceInstanceIdentifier, result.uid, result.buffer.revision(), adaptor), result.operation, result.aggregateValues);
            });
        if (!ret) {
            break;
        }
    };
    SinkTrace() << "Replayed " << counter << " results."
            << "Limit " << batchSize;
    return counter;
}

template class Sink::EntityReader<Sink::ApplicationDomain::Folder>;
template class Sink::EntityReader<Sink::ApplicationDomain::Mail>;
template class Sink::EntityReader<Sink::ApplicationDomain::Event>;
