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

#include "sink_export.h"
#include "facadeinterface.h"

#include <QByteArray>
#include <Async/Async>

#include "resourceaccess.h"
#include "domaintypeadaptorfactoryinterface.h"
#include "storage.h"
#include "resourcecontext.h"

namespace Sink {

/**
 * Default facade implementation for resources that are implemented in a separate process using the ResourceAccess class.
 *
 * Ideally a basic resource has no implementation effort for the facades and can simply instanciate default implementations (meaning it only has to implement the factory with all
 * supported types).
 * A resource has to implement:
 * * A facade factory registering all available facades
 * * An adaptor factory if it uses special resource buffers (default implementation can be used otherwise)
 * * A mapping between resource and buffertype if default can't be used.
 *
 * Additionally a resource only has to provide a synchronizer plugin to execute the synchronization
 */
template <typename DomainType>
class SINK_EXPORT GenericFacade : public Sink::StoreFacade<DomainType>
{
protected:
    SINK_DEBUG_AREA("facade")
    SINK_DEBUG_COMPONENT(mResourceContext.resourceInstanceIdentifier)
public:
    /**
     * Create a new GenericFacade
     *
     * @param resourceIdentifier is the identifier of the resource instance
     * @param adaptorFactory is the adaptor factory used to generate the mappings from domain to resource types and vice versa
     */
    GenericFacade(const ResourceContext &context);
    virtual ~GenericFacade();

    static QByteArray bufferTypeForDomainType();
    KAsync::Job<void> create(const DomainType &domainObject) Q_DECL_OVERRIDE;
    KAsync::Job<void> modify(const DomainType &domainObject) Q_DECL_OVERRIDE;
    KAsync::Job<void> move(const DomainType &domainObject, const QByteArray &newResource) Q_DECL_OVERRIDE;
    KAsync::Job<void> remove(const DomainType &domainObject) Q_DECL_OVERRIDE;
    virtual QPair<KAsync::Job<void>, typename ResultEmitter<typename DomainType::Ptr>::Ptr> load(const Sink::Query &query) Q_DECL_OVERRIDE;

protected:
    std::function<void(Sink::ApplicationDomain::ApplicationDomainType &domainObject)> mResultTransformation;
    ResourceContext mResourceContext;
    Sink::ResourceAccessInterface::Ptr mResourceAccess;
};

/**
 * A default facade implemenation that simply instantiates a generic resource
 */
template<typename DomainType>
class DefaultFacade : public GenericFacade<DomainType>
{
public:
    DefaultFacade(const ResourceContext &context) : GenericFacade<DomainType>(context) {}
    virtual ~DefaultFacade(){}
};

}
