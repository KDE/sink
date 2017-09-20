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
#pragma once

#include "sink_export.h"
#include <QObject>
#include <KAsync/Async>

#include "notification.h"
#include "resourcecontext.h"
#include "synchronizerstore.h"

namespace Sink {

/**
 * Synchronize and add what we don't already have to local queue
 */
class SINK_EXPORT Inspector : public QObject
{
    Q_OBJECT
public:
    Inspector(const ResourceContext &resourceContext);
    virtual ~Inspector();

    KAsync::Job<void> processCommand(void const *command, size_t size);

    void setSecret(const QString &s);

signals:
    void notify(Notification);

protected:
    QString secret() const;
    virtual KAsync::Job<void> inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue);

    Sink::ResourceContext mResourceContext;

private:
    QString mSecret;
};

}
