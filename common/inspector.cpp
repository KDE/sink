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
#include "inspector.h"

#include "resourcecontext.h"
#include "inspection_generated.h"
#include "bufferutils.h"

#include <QDataStream>

using namespace Sink;

Inspector::Inspector(const ResourceContext &context)
    : QObject(),
    mResourceContext(context)
    // mEntityStore(Storage::EntityStore::Ptr::create(mResourceContext)),
    // mSyncStorage(Sink::storageLocation(), mResourceContext.instanceId() + ".synchronization", Sink::Storage::DataStore::DataStore::ReadWrite)
{
    // SinkTrace() << "Starting synchronizer: " << mResourceContext.resourceType << mResourceContext.instanceId();
}

Inspector::~Inspector()
{

}

KAsync::Job<void> Inspector::processCommand(void const *command, size_t size)
{
    flatbuffers::Verifier verifier((const uint8_t *)command, size);
    if (Sink::Commands::VerifyInspectionBuffer(verifier)) {
        auto buffer = Sink::Commands::GetInspection(command);
        int inspectionType = buffer->type();

        QByteArray inspectionId = BufferUtils::extractBuffer(buffer->id());
        QByteArray entityId = BufferUtils::extractBuffer(buffer->entityId());
        QByteArray domainType = BufferUtils::extractBuffer(buffer->domainType());
        QByteArray property = BufferUtils::extractBuffer(buffer->property());
        QByteArray expectedValueString = BufferUtils::extractBuffer(buffer->expectedValue());
        QDataStream s(expectedValueString);
        QVariant expectedValue;
        s >> expectedValue;
        inspect(inspectionType, inspectionId, domainType, entityId, property, expectedValue)
            .then<void>(
                [=](const KAsync::Error &error) {
                    Sink::Notification n;
                    n.type = Sink::Notification::Inspection;
                    n.id = inspectionId;
                    if (error) {
                        Warning_area("resource.inspection") << "Inspection failed: " << inspectionType << inspectionId << entityId << error.errorMessage;
                        n.code = Sink::Notification::Failure;
                    } else {
                        Log_area("resource.inspection") << "Inspection was successful: " << inspectionType << inspectionId << entityId;
                        n.code = Sink::Notification::Success;
                    }
                    emit notify(n);
                    return KAsync::null();
                })
            .exec();
        return KAsync::null<void>();
    }
    return KAsync::error<void>(-1, "Invalid inspection command.");
}

KAsync::Job<void> Inspector::inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
{
    return KAsync::error(-1, "Inspection not implemented.");
}

