/*
 *   Copyright (C) 2015 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "common/genericresource.h"

#include <Async/Async>

#include <flatbuffers/flatbuffers.h>

//TODO: a little ugly to have this in two places, once here and once in Q_PLUGIN_METADATA
#define PLUGIN_NAME "org.kde.maildir"

class MaildirMailAdaptorFactory;
class MaildirFolderAdaptorFactory;

class MaildirResource : public Akonadi2::GenericResource
{
public:
    MaildirResource(const QByteArray &instanceIdentifier, const QSharedPointer<Akonadi2::Pipeline> &pipeline = QSharedPointer<Akonadi2::Pipeline>());
    KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE;
    static void removeFromDisk(const QByteArray &instanceIdentifier);
private:
    KAsync::Job<void> replay(const QByteArray &type, const QByteArray &key, const QByteArray &value) Q_DECL_OVERRIDE;

    /**
     * Records a localId to remoteId mapping
     */
    void recordRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId, Akonadi2::Storage::Transaction &transaction);
    void removeRemoteId(const QByteArray &bufferType, const QByteArray &localId, const QByteArray &remoteId, Akonadi2::Storage::Transaction &transaction);

    /**
     * Tries to find a local id for the remote id, and creates a new local id otherwise.
     * 
     * The new local id is recorded in the local to remote id mapping.
     */
    QString resolveRemoteId(const QByteArray &type, const QString &remoteId, Akonadi2::Storage::Transaction &transaction);

    /**
     * Tries to find a remote id for a local id.
     * 
     * This can fail if the entity hasn't been written back to the server yet.
     */
    QString resolveLocalId(const QByteArray &bufferType, const QByteArray &localId, Akonadi2::Storage::Transaction &transaction);

    /**
    * An algorithm to remove entities that are no longer existing.
    * 
    * This algorithm calls @param exists for every entity of type @param type, with its remoteId. For every entity where @param exists returns false,
    * an entity delete command is enqueued.
    */
    void scanForRemovals(Akonadi2::Storage::Transaction &transaction, Akonadi2::Storage::Transaction &synchronizationTransaction, const QByteArray &bufferType, std::function<bool(const QByteArray &remoteId)> exists);

    /**
     * An algorithm to create or modify the entity.
     *
     * Depending on whether the entity is locally available, or has changed.
     */
    void createOrModify(Akonadi2::Storage::Transaction &transaction, Akonadi2::Storage::Transaction &synchronizationTransaction, DomainTypeAdaptorFactoryInterface &adaptorFactory, const QByteArray &bufferType, const QByteArray &remoteId, const Akonadi2::ApplicationDomain::ApplicationDomainType &entity);

    void synchronizeFolders(Akonadi2::Storage::Transaction &transaction, Akonadi2::Storage::Transaction &synchronizationTransaction);
    void synchronizeMails(Akonadi2::Storage::Transaction &transaction, Akonadi2::Storage::Transaction &synchronizationTransaction, const QString &folder);
    QStringList listAvailableFolders();
    QString mMaildirPath;
    QSharedPointer<MaildirMailAdaptorFactory> mMailAdaptorFactory;
    QSharedPointer<MaildirFolderAdaptorFactory> mFolderAdaptorFactory;
};

class MaildirResourceFactory : public Akonadi2::ResourceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.maildir")
    Q_INTERFACES(Akonadi2::ResourceFactory)

public:
    MaildirResourceFactory(QObject *parent = 0);

    Akonadi2::Resource *createResource(const QByteArray &instanceIdentifier) Q_DECL_OVERRIDE;
    void registerFacades(Akonadi2::FacadeFactory &factory) Q_DECL_OVERRIDE;
};

