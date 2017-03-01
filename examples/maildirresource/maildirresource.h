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

#include <KAsync/Async>

#include <flatbuffers/flatbuffers.h>

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
    MaildirResource(const Sink::ResourceContext &resourceContext);

private:
    QStringList listAvailableFolders();
    QString mMaildirPath;
    QString mDraftsFolder;
};

class MaildirResourceFactory : public Sink::ResourceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "sink.maildir")
    Q_INTERFACES(Sink::ResourceFactory)

public:
    MaildirResourceFactory(QObject *parent = 0);

    Sink::Resource *createResource(const Sink::ResourceContext &context) Q_DECL_OVERRIDE;
    void registerFacades(const QByteArray &resourceName, Sink::FacadeFactory &factory) Q_DECL_OVERRIDE;
    void registerAdaptorFactories(const QByteArray &resourceName, Sink::AdaptorFactoryRegistry &registry) Q_DECL_OVERRIDE;
    void removeDataFromDisk(const QByteArray &instanceIdentifier) Q_DECL_OVERRIDE;
};

