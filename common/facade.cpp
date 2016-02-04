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

#include "facade.h"

#include "commands.h"
#include "log.h"
#include "storage.h"
#include "definitions.h"
#include "domainadaptor.h"
#include "queryrunner.h"
#include "bufferutils.h"

using namespace Sink;

#undef DEBUG_AREA
#define DEBUG_AREA "client.facade"

class ResourceAccessFactory {
public:
    static ResourceAccessFactory &instance()
    {
        static ResourceAccessFactory *instance = 0;
        if (!instance) {
            instance = new ResourceAccessFactory;
        }
        return *instance;
    }

    Sink::ResourceAccess::Ptr getAccess(const QByteArray &instanceIdentifier)
    {
        if (!mCache.contains(instanceIdentifier)) {
            //Reuse the pointer if something else kept the resourceaccess alive
            if (mWeakCache.contains(instanceIdentifier)) {
                auto sharedPointer = mWeakCache.value(instanceIdentifier).toStrongRef();
                if (sharedPointer) {
                    mCache.insert(instanceIdentifier, sharedPointer);
                }
            }
            if (!mCache.contains(instanceIdentifier)) {
                //Create a new instance if necessary
                auto sharedPointer = Sink::ResourceAccess::Ptr::create(instanceIdentifier);
                QObject::connect(sharedPointer.data(), &Sink::ResourceAccess::ready, sharedPointer.data(), [this, instanceIdentifier](bool ready) {
                    if (!ready) {
                        mCache.remove(instanceIdentifier);
                    }
                });
                mCache.insert(instanceIdentifier, sharedPointer);
                mWeakCache.insert(instanceIdentifier, sharedPointer);
            }
        }
        if (!mTimer.contains(instanceIdentifier)) {
            auto timer = new QTimer;
            //Drop connection after 3 seconds (which is a random value)
            QObject::connect(timer, &QTimer::timeout, timer, [this, instanceIdentifier]() {
                mCache.remove(instanceIdentifier);
            });
            timer->setInterval(3000);
            mTimer.insert(instanceIdentifier, timer);
        }
        auto timer = mTimer.value(instanceIdentifier);
        timer->start();
        return mCache.value(instanceIdentifier);
    }

    QHash<QByteArray, QWeakPointer<Sink::ResourceAccess> > mWeakCache;
    QHash<QByteArray, Sink::ResourceAccess::Ptr> mCache;
    QHash<QByteArray, QTimer*> mTimer;
};

template<class DomainType>
GenericFacade<DomainType>::GenericFacade(const QByteArray &resourceIdentifier, const DomainTypeAdaptorFactoryInterface::Ptr &adaptorFactory , const QSharedPointer<Sink::ResourceAccessInterface> resourceAccess)
    : Sink::StoreFacade<DomainType>(),
    mResourceAccess(resourceAccess),
    mDomainTypeAdaptorFactory(adaptorFactory),
    mResourceInstanceIdentifier(resourceIdentifier)
{
    if (!mResourceAccess) {
        mResourceAccess = ResourceAccessFactory::instance().getAccess(resourceIdentifier);
    }
}

template<class DomainType>
GenericFacade<DomainType>::~GenericFacade()
{
}

template<class DomainType>
QByteArray GenericFacade<DomainType>::bufferTypeForDomainType()
{
    //We happen to have a one to one mapping
    return Sink::ApplicationDomain::getTypeName<DomainType>();
}

template<class DomainType>
KAsync::Job<void> GenericFacade<DomainType>::create(const DomainType &domainObject)
{
    if (!mDomainTypeAdaptorFactory) {
        Warning() << "No domain type adaptor factory available";
        return KAsync::error<void>();
    }
    flatbuffers::FlatBufferBuilder entityFbb;
    mDomainTypeAdaptorFactory->createBuffer(domainObject, entityFbb);
    return mResourceAccess->sendCreateCommand(bufferTypeForDomainType(), BufferUtils::extractBuffer(entityFbb));
}

template<class DomainType>
KAsync::Job<void> GenericFacade<DomainType>::modify(const DomainType &domainObject)
{
    if (!mDomainTypeAdaptorFactory) {
        Warning() << "No domain type adaptor factory available";
        return KAsync::error<void>();
    }
    flatbuffers::FlatBufferBuilder entityFbb;
    mDomainTypeAdaptorFactory->createBuffer(domainObject, entityFbb);
    return mResourceAccess->sendModifyCommand(domainObject.identifier(), domainObject.revision(), bufferTypeForDomainType(), QByteArrayList(), BufferUtils::extractBuffer(entityFbb));
}

template<class DomainType>
KAsync::Job<void> GenericFacade<DomainType>::remove(const DomainType &domainObject)
{
    return mResourceAccess->sendDeleteCommand(domainObject.identifier(), domainObject.revision(), bufferTypeForDomainType());
}

template<class DomainType>
QPair<KAsync::Job<void>, typename ResultEmitter<typename DomainType::Ptr>::Ptr> GenericFacade<DomainType>::load(const Sink::Query &query)
{
    //The runner lives for the lifetime of the query
    auto runner = new QueryRunner<DomainType>(query, mResourceAccess, mResourceInstanceIdentifier, mDomainTypeAdaptorFactory, bufferTypeForDomainType());
    return qMakePair(KAsync::null<void>(), runner->emitter());
}


template class Sink::GenericFacade<Sink::ApplicationDomain::Folder>;
template class Sink::GenericFacade<Sink::ApplicationDomain::Mail>;
template class Sink::GenericFacade<Sink::ApplicationDomain::Event>;

#include "facade.moc"
