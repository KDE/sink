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
#include "resourceconfig.h"

using namespace Sink;

template <class DomainType>
GenericFacade<DomainType>::GenericFacade(const ResourceContext &context)
    : Sink::StoreFacade<DomainType>(), mResourceContext(context), mResourceAccess(mResourceContext.resourceAccess())
{
}

template <class DomainType>
GenericFacade<DomainType>::~GenericFacade()
{
}

template <class DomainType>
QByteArray GenericFacade<DomainType>::bufferTypeForDomainType()
{
    // We happen to have a one to one mapping
    return Sink::ApplicationDomain::getTypeName<DomainType>();
}

template <class DomainType>
KAsync::Job<void> GenericFacade<DomainType>::create(const DomainType &domainObject)
{
    flatbuffers::FlatBufferBuilder entityFbb;
    if (!mResourceContext.adaptorFactory<DomainType>().createBuffer(domainObject, entityFbb)) {
        SinkWarning() << "No domain type adaptor factory available";
        return KAsync::error<void>();
    }
    return mResourceAccess->sendCreateCommand(domainObject.identifier(), bufferTypeForDomainType(), BufferUtils::extractBuffer(entityFbb));
}

template <class DomainType>
KAsync::Job<void> GenericFacade<DomainType>::modify(const DomainType &domainObject)
{
    SinkTrace() << "Modifying entity: " << domainObject.identifier() << domainObject.changedProperties();
    flatbuffers::FlatBufferBuilder entityFbb;
    if (!mResourceContext.adaptorFactory<DomainType>().createBuffer(domainObject, entityFbb)) {
        SinkWarning() << "No domain type adaptor factory available";
        return KAsync::error<void>();
    }
    return mResourceAccess->sendModifyCommand(domainObject.identifier(), domainObject.revision(), bufferTypeForDomainType(), QByteArrayList(), BufferUtils::extractBuffer(entityFbb), domainObject.changedProperties(), QByteArray(), false);
}

template <class DomainType>
KAsync::Job<void> GenericFacade<DomainType>::move(const DomainType &domainObject, const QByteArray &newResource)
{
    SinkTrace() << "Moving entity: " << domainObject.identifier() << domainObject.changedProperties() << newResource;
    flatbuffers::FlatBufferBuilder entityFbb;
    if (!mResourceContext.adaptorFactory<DomainType>().createBuffer(domainObject, entityFbb)) {
        SinkWarning() << "No domain type adaptor factory available";
        return KAsync::error<void>();
    }
    return mResourceAccess->sendModifyCommand(domainObject.identifier(), domainObject.revision(), bufferTypeForDomainType(), QByteArrayList(), BufferUtils::extractBuffer(entityFbb), domainObject.changedProperties(), newResource, true);
}

template <class DomainType>
KAsync::Job<void> GenericFacade<DomainType>::copy(const DomainType &domainObject, const QByteArray &newResource)
{
    SinkTrace() << "Copying entity: " << domainObject.identifier() << domainObject.changedProperties() << newResource;
    flatbuffers::FlatBufferBuilder entityFbb;
    if (!mResourceContext.adaptorFactory<DomainType>().createBuffer(domainObject, entityFbb)) {
        SinkWarning() << "No domain type adaptor factory available";
        return KAsync::error<void>();
    }
    return mResourceAccess->sendModifyCommand(domainObject.identifier(), domainObject.revision(), bufferTypeForDomainType(), QByteArrayList(), BufferUtils::extractBuffer(entityFbb), domainObject.changedProperties(), newResource, false);
}

template <class DomainType>
KAsync::Job<void> GenericFacade<DomainType>::remove(const DomainType &domainObject)
{
    return mResourceAccess->sendDeleteCommand(domainObject.identifier(), domainObject.revision(), bufferTypeForDomainType());
}

template <class DomainType>
QPair<KAsync::Job<void>, typename ResultEmitter<typename DomainType::Ptr>::Ptr> GenericFacade<DomainType>::load(const Sink::Query &query, const Log::Context &ctx)
{
    Q_ASSERT(DomainType::name == query.type() || query.type().isEmpty());
    // The runner lives for the lifetime of the query
    auto runner = new QueryRunner<DomainType>(query, mResourceContext, bufferTypeForDomainType(), ctx);
    runner->setResultTransformation(mResultTransformation);
    return qMakePair(KAsync::null<void>(), runner->emitter());
}

#define REGISTER_TYPE(T) \
    template class Sink::GenericFacade<T>; \

SINK_REGISTER_TYPES()
