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
 * A maildir resource.
 * 
 * Implementation details:
 * The remoteid's have the following formats:
 * files: full file path
 * directories: full directory path
 * 
 * The resource moves all messages from new to cur during sync and thus expectes all messages that are in the store to always reside in cur.
 * The tmp directory is never directly used
 */
class MaildirResource : public Sink::GenericResource
{
public:
    MaildirResource(const QByteArray &instanceIdentifier, const QSharedPointer<Sink::Pipeline> &pipeline = QSharedPointer<Sink::Pipeline>());
    KAsync::Job<void> synchronizeWithSource(Sink::Storage &mainStore, Sink::Storage &synchronizationStore) Q_DECL_OVERRIDE;
    KAsync::Job<void> inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue) Q_DECL_OVERRIDE;
    static void removeFromDisk(const QByteArray &instanceIdentifier);
private:
    KAsync::Job<void> replay(Sink::Storage &synchronizationStore, const QByteArray &type, const QByteArray &key, const QByteArray &value) Q_DECL_OVERRIDE;

    void synchronizeFolders(Sink::Storage::Transaction &transaction, Sink::Storage::Transaction &synchronizationTransaction);
    void synchronizeMails(Sink::Storage::Transaction &transaction, Sink::Storage::Transaction &synchronizationTransaction, const QString &folder);
    QByteArray createFolder(const QString &folderPath, const QByteArray &icon, Sink::Storage::Transaction &transaction, Sink::Storage::Transaction &synchronizationTransaction);
    QStringList listAvailableFolders();
    QString mMaildirPath;
    QString mDraftsFolder;
    QSharedPointer<MaildirMailAdaptorFactory> mMailAdaptorFactory;
    QSharedPointer<MaildirFolderAdaptorFactory> mFolderAdaptorFactory;
};

class MaildirResourceFactory : public Sink::ResourceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.maildir")
    Q_INTERFACES(Sink::ResourceFactory)

public:
    MaildirResourceFactory(QObject *parent = 0);

    Sink::Resource *createResource(const QByteArray &instanceIdentifier) Q_DECL_OVERRIDE;
    void registerFacades(Sink::FacadeFactory &factory) Q_DECL_OVERRIDE;
};

