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
#include <resource.h>
#include <messagequeue.h>
#include <flatbuffers/flatbuffers.h>
#include <domainadaptor.h>
#include "changereplay.h"

#include <QTimer>

class CommandProcessor;

namespace Sink {
class Pipeline;
class Preprocessor;
class Synchronizer;

/**
 * Generic Resource implementation.
 */
class SINK_EXPORT GenericResource : public Resource
{
protected:
    SINK_DEBUG_AREA("resource")
public:
    GenericResource(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier, const QSharedPointer<Pipeline> &pipeline);
    virtual ~GenericResource();

    virtual void processCommand(int commandId, const QByteArray &data) Q_DECL_OVERRIDE;
    virtual KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE;
    virtual KAsync::Job<void> processAllMessages() Q_DECL_OVERRIDE;
    virtual void setLowerBoundRevision(qint64 revision) Q_DECL_OVERRIDE;
    virtual KAsync::Job<void>
    inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue);

    int error() const;

    void removeDataFromDisk() Q_DECL_OVERRIDE;
    static void removeFromDisk(const QByteArray &instanceIdentifier);
    static qint64 diskUsage(const QByteArray &instanceIdentifier);

private slots:
    void updateLowerBoundRevision();

protected:
    void enableChangeReplay(bool);

    void setupPreprocessors(const QByteArray &type, const QVector<Sink::Preprocessor *> &preprocessors);
    void setupSynchronizer(const QSharedPointer<Synchronizer> &synchronizer);
    void setupChangereplay(const QSharedPointer<ChangeReplay> &changeReplay);

    void onProcessorError(int errorCode, const QString &errorMessage);
    void enqueueCommand(MessageQueue &mq, int commandId, const QByteArray &data);

    MessageQueue mUserQueue;
    MessageQueue mSynchronizerQueue;
    QByteArray mResourceType;
    QByteArray mResourceInstanceIdentifier;
    QSharedPointer<Pipeline> mPipeline;

private:
    std::unique_ptr<CommandProcessor> mProcessor;
    QSharedPointer<ChangeReplay> mChangeReplay;
    QSharedPointer<Synchronizer> mSynchronizer;
    int mError;
    QTimer mCommitQueueTimer;
    qint64 mClientLowerBoundRevision;
};

}
