/*
 * Copyright (C) 2016 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include <QTimer>
#include <Async/Async>
#include <functional>

#include "log.h"
#include "notification.h"
#include "messagequeue.h"

namespace Sink {
    class Pipeline;
    class Inspector;
    class Synchronizer;
    struct QueuedCommand;
    class QueryBase;

/**
 * Drives the pipeline using the output from all command queues
 */
class CommandProcessor : public QObject
{
    Q_OBJECT

public:
    CommandProcessor(Sink::Pipeline *pipeline, const QByteArray &instanceId, const Sink::Log::Context &ctx);

    void setOldestUsedRevision(qint64 revision);

    void setInspector(const QSharedPointer<Inspector> &inspector);
    void setSynchronizer(const QSharedPointer<Synchronizer> &synchronizer);

    void processCommand(int commandId, const QByteArray &data);

    KAsync::Job<void> processAllMessages();

signals:
    void notify(Notification);
    void error(int errorCode, const QString &errorMessage);

private:
    bool messagesToProcessAvailable();

private slots:
    void process();
    KAsync::Job<qint64> processQueuedCommand(const Sink::QueuedCommand *queuedCommand);
    KAsync::Job<qint64> processQueuedCommand(const QByteArray &data);
    // Process all messages of this queue
    KAsync::Job<void> processQueue(MessageQueue *queue);
    KAsync::Job<void> processPipeline();

private:
    void processFlushCommand(const QByteArray &data);
    void processSynchronizeCommand(const QByteArray &data);
    // void processRevisionReplayedCommand(const QByteArray &data);

    KAsync::Job<void> flush(void const *command, size_t size);

    Sink::Log::Context mLogCtx;
    Sink::Pipeline *mPipeline;
    MessageQueue mUserQueue;
    MessageQueue mSynchronizerQueue;
    // Ordered by priority
    QList<MessageQueue *> mCommandQueues;
    bool mProcessingLock;
    // The lowest revision we no longer need
    qint64 mLowerBoundRevision;
    QSharedPointer<Synchronizer> mSynchronizer;
    QSharedPointer<Inspector> mInspector;
    QTimer mCommitQueueTimer;
};

};
