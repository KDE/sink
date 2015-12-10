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

#include <akonadi2common_export.h>
#include <resource.h>
#include <messagequeue.h>
#include <flatbuffers/flatbuffers.h>
#include <QTimer>

class Processor;
class ChangeReplay;

namespace Akonadi2
{
class Pipeline;

/**
 * Generic Resource implementation.
 */
class AKONADI2COMMON_EXPORT GenericResource : public Resource
{
public:
    GenericResource(const QByteArray &resourceInstanceIdentifier, const QSharedPointer<Pipeline> &pipeline = QSharedPointer<Pipeline>());
    virtual ~GenericResource();

    virtual void processCommand(int commandId, const QByteArray &data) Q_DECL_OVERRIDE;
    virtual KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE = 0;
    virtual KAsync::Job<void> processAllMessages() Q_DECL_OVERRIDE;
    virtual void setLowerBoundRevision(qint64 revision) Q_DECL_OVERRIDE;

    int error() const;

    static void removeFromDisk(const QByteArray &instanceIdentifier);
private Q_SLOTS:
    void updateLowerBoundRevision();

protected:
    virtual KAsync::Job<void> replay(const QByteArray &type, const QByteArray &key, const QByteArray &value);
    void onProcessorError(int errorCode, const QString &errorMessage);
    void enqueueCommand(MessageQueue &mq, int commandId, const QByteArray &data);
    flatbuffers::FlatBufferBuilder m_fbb;
    MessageQueue mUserQueue;
    MessageQueue mSynchronizerQueue;
    QByteArray mResourceInstanceIdentifier;
    QSharedPointer<Pipeline> mPipeline;

private:
    Processor *mProcessor;
    ChangeReplay *mSourceChangeReplay;
    int mError;
    QTimer mCommitQueueTimer;
    qint64 mClientLowerBoundRevision;
};

}
