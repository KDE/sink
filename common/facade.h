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

namespace Akonadi2 {
    class ResourceAccess;
/**
 * Default facade implementation for resources that are implemented in a separate process using the ResourceAccess class.
 */
template <typename DomainType>
class GenericFacade: public Akonadi2::StoreFacade<DomainType>
{
public:
    GenericFacade(const QByteArray &resourceIdentifier)
        : Akonadi2::StoreFacade<DomainType>(),
        mResourceAccess(new ResourceAccess(resourceIdentifier))
    {
    }

    ~GenericFacade()
    {
    }

protected:
    Async::Job<void> sendCreateCommand(const QByteArray &t, const QByteArray &buffer)
    {
        flatbuffers::FlatBufferBuilder fbb;
        //This is the resource buffer type and not the domain type
        auto type = fbb.CreateString(t.constData());
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

private:
    //TODO use one resource access instance per application => make static
    QSharedPointer<Akonadi2::ResourceAccess> mResourceAccess;
};

}
