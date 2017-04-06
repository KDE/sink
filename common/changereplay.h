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
#include <KAsync/Async>

#include "storage.h"
#include "resourcecontext.h"

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
    ChangeReplay(const ResourceContext &resourceContext, const Sink::Log::Context &ctx= {});

    qint64 getLastReplayedRevision();
    virtual bool allChangesReplayed();

signals:
    void changesReplayed();
    void replayingChanges();

public slots:
    virtual void revisionChanged();

protected:
    virtual KAsync::Job<void> replay(const QByteArray &type, const QByteArray &key, const QByteArray &value) = 0;
    virtual bool canReplay(const QByteArray &type, const QByteArray &key, const QByteArray &value) = 0;
    Sink::Storage::DataStore mStorage;
    KAsync::Job<void> replayNextRevision();

private:
    void recordReplayedRevision(qint64 revision);
    Sink::Storage::DataStore mChangeReplayStore;
    bool mReplayInProgress;
    Sink::Storage::DataStore::Transaction mMainStoreTransaction;
    Sink::Log::Context mLogCtx;
    QSharedPointer<QObject> mGuard;
};

class NullChangeReplay : public ChangeReplay
{
public:
    NullChangeReplay(const ResourceContext &resourceContext) : ChangeReplay(resourceContext) {}
    KAsync::Job<void> replay(const QByteArray &type, const QByteArray &key, const QByteArray &value) Q_DECL_OVERRIDE { return KAsync::null<void>(); }
    bool canReplay(const QByteArray &type, const QByteArray &key, const QByteArray &value) Q_DECL_OVERRIDE { return false; }
};

}

