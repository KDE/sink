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

/**
 * A maildir resource
 * 
 * Implementation details:
 * The remoteid's have the following formats:
 * files: full file path
 * directories: full directory path
 */
class MaildirResource : public Akonadi2::GenericResource
{
public:
    MaildirResource(const QByteArray &instanceIdentifier, const QSharedPointer<Akonadi2::Pipeline> &pipeline = QSharedPointer<Akonadi2::Pipeline>());
    KAsync::Job<void> synchronizeWithSource(Akonadi2::Storage &mainStore, Akonadi2::Storage &synchronizationStore) Q_DECL_OVERRIDE;
    KAsync::Job<void> inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue) Q_DECL_OVERRIDE;
    static void removeFromDisk(const QByteArray &instanceIdentifier);
private:
    KAsync::Job<void> replay(Akonadi2::Storage &synchronizationStore, const QByteArray &type, const QByteArray &key, const QByteArray &value) Q_DECL_OVERRIDE;

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

