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

#include "common/resource.h"
#include "common/genericresource.h"

/**
 * An imap resource.
 */
class ImapResource : public Sink::GenericResource
{
public:
    ImapResource(const Sink::ResourceContext &resourceContext);
};

class ImapResourceFactory : public Sink::ResourceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "sink.imap")
    Q_INTERFACES(Sink::ResourceFactory)

public:
    ImapResourceFactory(QObject *parent = 0);

    Sink::Resource *createResource(const Sink::ResourceContext &resourceContext) Q_DECL_OVERRIDE;
    void registerFacades(const QByteArray &name, Sink::FacadeFactory &factory) Q_DECL_OVERRIDE;
    void registerAdaptorFactories(const QByteArray &name, Sink::AdaptorFactoryRegistry &registry) Q_DECL_OVERRIDE;
    void removeDataFromDisk(const QByteArray &instanceIdentifier) Q_DECL_OVERRIDE;
};

