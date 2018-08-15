/*
    Copyright (c) 2016 Christian Mollekopf <mollekopf@kolabsys.com>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#pragma once

#include <QByteArray>
#include <QFlag>
#include <KMime/Message>

namespace MailTransport
{
    enum Option {
        UseTls = 1,
        UseStarttls = 2,
        VerifyPeers = 4
    };
    Q_DECLARE_FLAGS(Options, Option);

    struct SendResult {
        bool error;
        QString errorMessage;
    };

    /*
     * For ssl use "smtps://mainserver.example.net
     * @param cacert: "/path/to/certificate.pem";
     */
    SendResult sendMessage(const KMime::Message::Ptr &message, const QByteArray &server, const QByteArray &username, const QByteArray &password, const QByteArray &cacert, Options flags);
};
Q_DECLARE_OPERATORS_FOR_FLAGS(MailTransport::Options)
