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
#include <resourcecontext.h>

namespace Sink {
class Pipeline;
class Preprocessor;
class Synchronizer;
class Inspector;
class CommandProcessor;

/**
 * Generic Resource implementation.
 */
class SINK_EXPORT GenericResource : public Resource
{
public:
    GenericResource(const Sink::ResourceContext &context, const QSharedPointer<Pipeline> &pipeline = QSharedPointer<Pipeline>());
    virtual ~GenericResource() Q_DECL_OVERRIDE;

    virtual void processCommand(int commandId, const QByteArray &data) Q_DECL_OVERRIDE;
    virtual void setLowerBoundRevision(qint64 revision) Q_DECL_OVERRIDE;

    int error() const;

    static void removeFromDisk(const QByteArray &instanceIdentifier);
    static qint64 diskUsage(const QByteArray &instanceIdentifier);

    virtual void setSecret(const QString &s) Q_DECL_OVERRIDE;
    virtual bool checkForUpgrade() Q_DECL_OVERRIDE;

    //TODO Remove this API, it's only used in tests
    KAsync::Job<void> synchronizeWithSource(const Sink::QueryBase &query);
    //TODO Remove this API, it's only used in tests
    KAsync::Job<void> processAllMessages();

private slots:
    void updateLowerBoundRevision();

protected:
    void setupPreprocessors(const QByteArray &type, const QVector<Sink::Preprocessor *> &preprocessors);
    void setupSynchronizer(const QSharedPointer<Synchronizer> &synchronizer);
    void setupInspector(const QSharedPointer<Inspector> &inspector);

    ResourceContext mResourceContext;

private:
    void onProcessorError(int errorCode, const QString &errorMessage);

    QSharedPointer<Pipeline> mPipeline;
    QSharedPointer<CommandProcessor> mProcessor;
    QSharedPointer<Synchronizer> mSynchronizer;
    QSharedPointer<Inspector> mInspector;
    int mError;
    qint64 mClientLowerBoundRevision;
};

}
