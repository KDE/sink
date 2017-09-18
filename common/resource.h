/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
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

#include <KAsync/Async>
#include "notification.h"

namespace Sink {
class FacadeFactory;
class AdaptorFactoryRegistry;
struct ResourceContext;
class QueryBase;

/**
 * Resource interface
 */
class SINK_EXPORT Resource : public QObject
{
    Q_OBJECT
public:
    Resource();
    virtual ~Resource();

    virtual void processCommand(int commandId, const QByteArray &data);

    /**
     * Set the lowest revision that is still referenced by external clients.
     */
    virtual void setLowerBoundRevision(qint64 revision);

    virtual void setSecret(const QString &s);

signals:
    void revisionUpdated(qint64);
    void notify(Notification);

private:
    class Private;
    Private *const d;
};

/**
 * Factory interface for resource to implement.
 */
class SINK_EXPORT ResourceFactory : public QObject
{
public:
    static ResourceFactory *load(const QByteArray &resourceName);

    ResourceFactory(QObject *parent, const QByteArrayList &capabilities);
    virtual ~ResourceFactory();

    virtual Resource *createResource(const ResourceContext &context) = 0;
    virtual void registerFacades(const QByteArray &resourceName, FacadeFactory &factory) = 0;
    virtual void registerAdaptorFactories(const QByteArray &resourceName, AdaptorFactoryRegistry &registry) {};
    virtual void removeDataFromDisk(const QByteArray &instanceIdentifier) = 0;
    QByteArrayList capabilities() const;

private:
    class Private;
    Private *const d;
};

} // namespace Sink

Q_DECLARE_INTERFACE(Sink::ResourceFactory, "sink.sink.resourcefactory")
