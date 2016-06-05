/*
 *   Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include "facade.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <KMime/Message>

#include "resultprovider.h"
#include "mailtransport.h"
#include <log.h>
#include <resourceconfig.h>

static QString dataDirectory(const QByteArray &identifier)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/sink/mailtransport/" + identifier;
}

class Outbox {
public:
    Outbox(const QByteArray &identifier) : mIdentifier(identifier)
    {

    }

    static QString fileName(const QByteArray &resourceId, const QByteArray &messageId)
    {
        return dataDirectory(resourceId)+"/" + messageId;
    }

    void add(const QByteArray &messageId, const QString &messagePath, QMap<QByteArray, QString> config)
    {
        QDir dir;
        dir.mkpath(dataDirectory(mIdentifier));
        if (!QFile(messagePath).rename(fileName(mIdentifier, messageId))) {
            ErrorMsg() << "Failed to move the file:";
            ErrorMsg() << messagePath << " to " << fileName(mIdentifier, messageId);
        }
        //TODO store settings
        // QSettings settings(dataDirectory(mIdentifier) + "/messageId.ini", QSettings::IniFormat);
    }

    void dispatch(const QByteArray &messageId)
    {
        QFile mimeMessage(fileName(mIdentifier, messageId));
        if (!mimeMessage.open(QIODevice::ReadOnly)) {
            ErrorMsg() << "Failed to open mime message: " << mimeMessage.errorString();
            ErrorMsg() << fileName(mIdentifier, messageId);
            return;
        }

        auto msg = KMime::Message::Ptr::create();
        msg->setHead(KMime::CRLFtoLF(mimeMessage.readAll()));
        msg->parse();
        MailTransport::sendMessage(msg, mServer, mUsername, mPassword, mCaCert);
        Trace() << "Sent message: " << msg->subject();
    }

    void setServer(const QByteArray &server, const QByteArray &username, const QByteArray &caCert)
    {
        mServer = server;
        mUsername = username;
        mCaCert = caCert;
    }

    void setPassword(const QByteArray &password)
    {
        mPassword = password;
    }

private:
    QByteArray mServer;
    QByteArray mUsername;
    QByteArray mPassword;
    QByteArray mCaCert;
    QByteArray mIdentifier;
};

MailtransportFacade::MailtransportFacade(const QByteArray &identifier) : Sink::StoreFacade<Sink::ApplicationDomain::Mail>(), mIdentifier(identifier)
{
}

MailtransportFacade::~MailtransportFacade()
{
}

KAsync::Job<void> MailtransportFacade::create(const Sink::ApplicationDomain::Mail &mail)
{
    Trace() << "Called create: ";
    return KAsync::start<void>([mail, this]() {
        auto config = ResourceConfig::getConfiguration(mIdentifier);

        auto identifier = Sink::Storage::generateUid();
        Trace() << "Sending new message: " << identifier;
        Trace() << config.value("server").toByteArray() << config.value("username").toByteArray() << config.value("cacert").toByteArray();

        Outbox outbox(mIdentifier);
        outbox.setServer(config.value("server").toByteArray(), config.value("username").toByteArray(), config.value("cacert").toByteArray());
        //FIXME remove and somehow retrieve the password on demand
        outbox.setPassword(config.value("password").toByteArray());

        const QByteArray mimeMessage = mail.getProperty("mimeMessage").toByteArray();
        QMap<QByteArray, QString> configurationValues;
        outbox.add(identifier, mimeMessage, configurationValues);
        outbox.dispatch(identifier);
    });
}

KAsync::Job<void> MailtransportFacade::modify(const Sink::ApplicationDomain::Mail &mail)
{
    return KAsync::error<void>(0, "Not implemented.");
}

KAsync::Job<void> MailtransportFacade::remove(const Sink::ApplicationDomain::Mail &mail)
{
    return KAsync::error<void>(0, "Not implemented.");
}

QPair<KAsync::Job<void>, typename Sink::ResultEmitter<Sink::ApplicationDomain::Mail::Ptr>::Ptr> MailtransportFacade::load(const Sink::Query &query)
{
    return qMakePair(KAsync::error<void>(0, "Not implemented."), Sink::ResultEmitter<Sink::ApplicationDomain::Mail::Ptr>::Ptr());
}
