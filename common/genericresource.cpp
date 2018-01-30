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
#include "genericresource.h"

#include "pipeline.h"
#include "synchronizer.h"
#include "inspector.h"
#include "commandprocessor.h"
#include "definitions.h"
#include "storage.h"

using namespace Sink;
using namespace Sink::Storage;

GenericResource::GenericResource(const ResourceContext &resourceContext, const QSharedPointer<Pipeline> &pipeline )
    : Sink::Resource(),
      mResourceContext(resourceContext),
      mPipeline(pipeline ? pipeline : QSharedPointer<Sink::Pipeline>::create(resourceContext, Log::Context{})),
      mProcessor(QSharedPointer<CommandProcessor>::create(mPipeline.data(), resourceContext.instanceId(), Log::Context{})),
      mError(0),
      mClientLowerBoundRevision(std::numeric_limits<qint64>::max())
{
    QObject::connect(mProcessor.data(), &CommandProcessor::error, [this](int errorCode, const QString &msg) { onProcessorError(errorCode, msg); });
    QObject::connect(mProcessor.data(), &CommandProcessor::notify, this, &GenericResource::notify);
    QObject::connect(mPipeline.data(), &Pipeline::revisionUpdated, this, &Resource::revisionUpdated);
}

GenericResource::~GenericResource()
{
}

void GenericResource::setSecret(const QString &s)
{
    if (mSynchronizer) {
        mSynchronizer->setSecret(s);
    }
    if (mInspector) {
        mInspector->setSecret(s);
    }
}

bool GenericResource::checkForUpgrade()
{
    const auto currentDatabaseVersion = [&] {
        auto store = Sink::Storage::DataStore(Sink::storageLocation(), mResourceContext.instanceId(), Sink::Storage::DataStore::ReadOnly);
        return Storage::DataStore::databaseVersion(store.createTransaction(Storage::DataStore::ReadOnly));
    }();
    if (currentDatabaseVersion < Sink::latestDatabaseVersion()) {
        SinkLog() << "Starting database upgrade from " << currentDatabaseVersion << " to " << Sink::latestDatabaseVersion();

        //Right now upgrading just means removing all local storage so we will resync
        Sink::Storage::DataStore(Sink::storageLocation(), mResourceContext.instanceId(), Sink::Storage::DataStore::ReadWrite).removeFromDisk();
        Sink::Storage::DataStore(Sink::storageLocation(), mResourceContext.instanceId() + ".userqueue", Sink::Storage::DataStore::ReadWrite).removeFromDisk();
        Sink::Storage::DataStore(Sink::storageLocation(), mResourceContext.instanceId() + ".synchronizerqueue", Sink::Storage::DataStore::ReadWrite).removeFromDisk();
        Sink::Storage::DataStore(Sink::storageLocation(), mResourceContext.instanceId() + ".changereplay", Sink::Storage::DataStore::ReadWrite).removeFromDisk();
        Sink::Storage::DataStore(Sink::storageLocation(), mResourceContext.instanceId() + ".synchronization", Sink::Storage::DataStore::ReadWrite).removeFromDisk();

        {
            auto store = Sink::Storage::DataStore(Sink::storageLocation(), mResourceContext.instanceId(), Sink::Storage::DataStore::ReadWrite);
            auto t = store.createTransaction(Storage::DataStore::ReadWrite);
            Storage::DataStore::setDatabaseVersion(t, Sink::latestDatabaseVersion());
        }
        SinkLog() << "Finished database upgrade to " << Sink::latestDatabaseVersion();
        return true;
    }
    return false;
}

void GenericResource::setupPreprocessors(const QByteArray &type, const QVector<Sink::Preprocessor *> &preprocessors)
{
    mPipeline->setPreprocessors(type, preprocessors);
}

void GenericResource::setupSynchronizer(const QSharedPointer<Synchronizer> &synchronizer)
{
    mSynchronizer = synchronizer;
    mProcessor->setSynchronizer(synchronizer);
    QObject::connect(mPipeline.data(), &Pipeline::revisionUpdated, mSynchronizer.data(), &ChangeReplay::revisionChanged, Qt::QueuedConnection);
    QObject::connect(mSynchronizer.data(), &ChangeReplay::changesReplayed, this, &GenericResource::updateLowerBoundRevision);
    QMetaObject::invokeMethod(mSynchronizer.data(), "revisionChanged", Qt::QueuedConnection);
}

void GenericResource::setupInspector(const QSharedPointer<Inspector> &inspector)
{
    mInspector = inspector;
    mProcessor->setInspector(inspector);
}

void GenericResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier, Sink::Storage::DataStore::ReadWrite).removeFromDisk();
    Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".userqueue", Sink::Storage::DataStore::ReadWrite).removeFromDisk();
    Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".synchronizerqueue", Sink::Storage::DataStore::ReadWrite).removeFromDisk();
    Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".changereplay", Sink::Storage::DataStore::ReadWrite).removeFromDisk();
    Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".synchronization", Sink::Storage::DataStore::ReadWrite).removeFromDisk();
}

qint64 GenericResource::diskUsage(const QByteArray &instanceIdentifier)
{
    auto size = Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier, Sink::Storage::DataStore::ReadOnly).diskUsage();
    size += Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".userqueue", Sink::Storage::DataStore::ReadOnly).diskUsage();
    size += Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".synchronizerqueue", Sink::Storage::DataStore::ReadOnly).diskUsage();
    size += Sink::Storage::DataStore(Sink::storageLocation(), instanceIdentifier + ".changereplay", Sink::Storage::DataStore::ReadOnly).diskUsage();
    return size;
}

void GenericResource::onProcessorError(int errorCode, const QString &errorMessage)
{
    SinkWarning() << "Received error from Processor: " << errorCode << errorMessage;
    mError = errorCode;
}

int GenericResource::error() const
{
    return mError;
}

void GenericResource::processCommand(int commandId, const QByteArray &data)
{
    mProcessor->processCommand(commandId, data);
}

KAsync::Job<void> GenericResource::synchronizeWithSource(const Sink::QueryBase &query)
{
    mSynchronizer->synchronize(query);
    return KAsync::null<void>();
}

KAsync::Job<void> GenericResource::processAllMessages()
{
    return mProcessor->processAllMessages();
}

void GenericResource::updateLowerBoundRevision()
{
    mProcessor->setOldestUsedRevision(qMin(mClientLowerBoundRevision, mSynchronizer->getLastReplayedRevision()));
}

void GenericResource::setLowerBoundRevision(qint64 revision)
{
    mClientLowerBoundRevision = revision;
    updateLowerBoundRevision();
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#include "genericresource.moc"
#pragma clang diagnostic pop
