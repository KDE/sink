/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
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
#include "common/messagequeue.h"

#include <Async/Async>

#include <flatbuffers/flatbuffers.h>

//TODO: a little ugly to have this in two places, once here and once in Q_PLUGIN_METADATA
#define PLUGIN_NAME "org.kde.dummy"

class DummyEventAdaptorFactory;
class DummyMailAdaptorFactory;
class DummyFolderAdaptorFactory;

class DummyResource : public Akonadi2::GenericResource
{
public:
    DummyResource(const QByteArray &instanceIdentifier, const QSharedPointer<Akonadi2::Pipeline> &pipeline = QSharedPointer<Akonadi2::Pipeline>());
    KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE;
    static void removeFromDisk(const QByteArray &instanceIdentifier);
private:
    KAsync::Job<void> replay(const QByteArray &type, const QByteArray &key, const QByteArray &value) Q_DECL_OVERRIDE;
    QString resolveRemoteId(const QByteArray &type, const QString &remoteId, Akonadi2::Storage::Transaction &transaction);
    void createEvent(const QByteArray &rid, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &);
    void createMail(const QByteArray &rid, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &);
    void createFolder(const QByteArray &rid, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &);
    void synchronize(const QString &bufferType, const QMap<QString, QMap<QString, QVariant> > &data, Akonadi2::Storage::Transaction &transaction, std::function<void(const QByteArray &ridBuffer, const QMap<QString, QVariant> &data, flatbuffers::FlatBufferBuilder &entityFbb, Akonadi2::Storage::Transaction &)> createEntity);

    QSharedPointer<DummyEventAdaptorFactory> mEventAdaptorFactory;
    QSharedPointer<DummyMailAdaptorFactory> mMailAdaptorFactory;
    QSharedPointer<DummyFolderAdaptorFactory> mFolderAdaptorFactory;
};

class DummyResourceFactory : public Akonadi2::ResourceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.dummy")
    Q_INTERFACES(Akonadi2::ResourceFactory)

public:
    DummyResourceFactory(QObject *parent = 0);

    Akonadi2::Resource *createResource(const QByteArray &instanceIdentifier) Q_DECL_OVERRIDE;
    void registerFacades(Akonadi2::FacadeFactory &factory) Q_DECL_OVERRIDE;
};

