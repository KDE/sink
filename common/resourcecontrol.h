/*
 * Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#pragma once

#include "sink_export.h"
#include <QByteArray>

#include <Async/Async>

#include "inspection.h"

namespace Sink {
namespace ResourceControl {

template <class DomainType>
KAsync::Job<void> SINK_EXPORT inspect(const Inspection &inspectionCommand);

/**
 * Shutdown resource.
 */
KAsync::Job<void> SINK_EXPORT shutdown(const QByteArray &resourceIdentifier);

/**
 * Start resource.
 * 
 * The resource is ready for operation once this command completes.
 * This command is only necessary if a resource was shutdown previously,
 * otherwise the resource process will automatically start as necessary.
 */
KAsync::Job<void> SINK_EXPORT start(const QByteArray &resourceIdentifier);

/**
 * Flushes any pending messages to disk
 */
KAsync::Job<void> SINK_EXPORT flushMessageQueue(const QByteArrayList &resourceIdentifier);

/**
 * Flushes any pending messages that haven't been replayed to the source.
 */
KAsync::Job<void> SINK_EXPORT flushReplayQueue(const QByteArrayList &resourceIdentifier);

    }
}

