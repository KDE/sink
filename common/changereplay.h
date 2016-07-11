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
#pragma once

#include "sink_export.h"
#include <QObject>
#include <Async/Async>

#include "storage.h"

namespace Sink {

/**
 * Replays changes from the storage one by one.
 *
 * Uses a local database to:
 * * Remember what changes have been replayed already.
 * * store a mapping of remote to local buffers
 */
class SINK_EXPORT ChangeReplay : public QObject
{
    Q_OBJECT
public:
    ChangeReplay(const QByteArray &resourceName);

    qint64 getLastReplayedRevision();
    bool allChangesReplayed();

signals:
    void changesReplayed();
    void replayingChanges();

public slots:
    void revisionChanged();

protected:
    virtual KAsync::Job<void> replay(const QByteArray &type, const QByteArray &key, const QByteArray &value) = 0;
    Sink::Storage mStorage;

private:
    KAsync::Job<void> replayNextRevision();
    Sink::Storage mChangeReplayStore;
    bool mReplayInProgress;
};

class NullChangeReplay : public ChangeReplay
{
public:
    NullChangeReplay(const QByteArray &resourceName) : ChangeReplay(resourceName) {}
    KAsync::Job<void> replay(const QByteArray &type, const QByteArray &key, const QByteArray &value) Q_DECL_OVERRIDE { return KAsync::null<void>(); }
};

}

