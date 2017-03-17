/*
 * Copyright (c) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
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
#include <QString>
#include <QDebug>

namespace Sink {

/**
 * A notification
 */
class SINK_EXPORT Notification
{
public:
    enum NoticationType {
        Shutdown,
        Status,
        Info,
        Warning,
        Error,
        Progress,
        Inspection,
        RevisionUpdate,
        FlushCompletion
    };
    /**
     * Used as code for Inspection type notifications
     */
    enum InspectionCode {
        Success = 0,
        Failure
    };
    /**
     * Used as code for Warning/Error type notifications
     */
    enum ErrorCode {
        NoError = 0,
        UnknownError,
        NoServerAvailable,
        LoginFailed,
        TransmissionFailed,
    };
    /**
     * Used as code for Info type notifications
     */
    enum SuccessCode {
        TransmissionSucceeded
    };

    QByteArray id;
    int type = 0;
    QString message;
    //A return code. Zero typically indicates success.
    int code = 0;
    QByteArray resource;
};
}

SINK_EXPORT QDebug operator<<(QDebug dbg, const Sink::Notification &n);
