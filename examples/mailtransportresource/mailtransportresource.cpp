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

#include "mailtransportresource.h"
#include "facade.h"
#include "facadefactory.h"
#include "resourceconfig.h"
#include "definitions.h"
#include "domainadaptor.h"
#include "sourcewriteback.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <KMime/Message>

#include "resultprovider.h"
#include "mailtransport.h"
#include "mail_generated.h"
#include <synchronizer.h>
#include <log.h>
#include <resourceconfig.h>
#include <pipeline.h>

#define ENTITY_TYPE_MAIL "mail"

using namespace Sink;

class MimeMessageMover : public Sink::EntityPreprocessor<ApplicationDomain::Mail>
{
public:
    MimeMessageMover(const QByteArray &resourceInstanceIdentifier) : Sink::EntityPreprocessor<ApplicationDomain::Mail>(), mResourceInstanceIdentifier(resourceInstanceIdentifier) {}

    QString moveMessage(const QString &oldPath, const Sink::ApplicationDomain::Mail &mail)
    {
        const auto directory = Sink::resourceStorageLocation(mResourceInstanceIdentifier);
        const auto filePath = directory + "/" + mail.identifier();
        if (oldPath != filePath) {
            if (!QDir().mkpath(directory)) {
                Warning() << "Failed to create the directory: " << directory;
            }
            QFile::remove(filePath);
            QFile origFile(oldPath);
            if (!origFile.open(QIODevice::ReadWrite)) {
                Warning() << "Failed to open the original file with write rights: " << origFile.errorString();
            }
            if (!origFile.rename(filePath)) {
                Warning() << "Failed to move the file from: " << oldPath << " to " << filePath << ". " << origFile.errorString();
            }
            origFile.close();
            return filePath;
        }
        return oldPath;
    }

    void newEntity(Sink::ApplicationDomain::Mail &mail, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        if (!mail.getMimeMessagePath().isEmpty()) {
            mail.setMimeMessagePath(moveMessage(mail.getMimeMessagePath(), mail));
        }
    }

    void modifiedEntity(const Sink::ApplicationDomain::Mail &oldMail, Sink::ApplicationDomain::Mail &newMail, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        if (!newMail.getMimeMessagePath().isEmpty()) {
            newMail.setMimeMessagePath(moveMessage(newMail.getMimeMessagePath(), newMail));
        }
    }

    void deletedEntity(const Sink::ApplicationDomain::Mail &mail, Sink::Storage::Transaction &transaction) Q_DECL_OVERRIDE
    {
        QFile::remove(mail.getMimeMessagePath());
    }
    QByteArray mResourceInstanceIdentifier;
};

static KAsync::Job<void>send(const ApplicationDomain::Mail &mail, const MailtransportResource::Settings &settings)
{
    const auto data = mail.getMimeMessage();
    auto msg = KMime::Message::Ptr::create();
    msg->setHead(KMime::CRLFtoLF(data));
    msg->parse();
    if (settings.testMode) {
        Log() << "I would totally send that mail, but I'm in test mode.";
    } else {
        if (MailTransport::sendMessage(msg, settings.server.toUtf8(), settings.username.toUtf8(), settings.password.toUtf8(), settings.cacert.toUtf8())) {
            Log() << "Sent message successfully";
        } else {
            Log() << "Failed to send message";
            return KAsync::error<void>(1, "Failed to send the message.");
        }
    }
    return KAsync::null<void>();
}

//TODO fold into synchronizer
class MailtransportWriteback : public Sink::SourceWriteBack
{
public:
    MailtransportWriteback(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier) : Sink::SourceWriteBack(resourceType, resourceInstanceIdentifier)
    {

    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Mail &mail, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        if (operation == Sink::Operation_Creation) {
            Trace() << "Dispatching message.";
            // return send(mail, mSettings);
        } else if (operation == Sink::Operation_Removal) {
        } else if (operation == Sink::Operation_Modification) {
        }
        return KAsync::null<QByteArray>();
    }

public:
    MailtransportResource::Settings mSettings;
};

class MailtransportSynchronizer : public Sink::Synchronizer {
public:
    MailtransportSynchronizer(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier)
        : Sink::Synchronizer(resourceType, resourceInstanceIdentifier)
    {

    }

    KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE
    {
        Log() << " Synchronizing";
        return KAsync::start<void>([this](KAsync::Future<void> future) {
            Sink::Query query;
            QList<ApplicationDomain::Mail> toSend;
            Log() << " Looking for mail";
            store().reader<ApplicationDomain::Mail>().query(query, [&](const ApplicationDomain::Mail &mail) -> bool {
                Trace() << "Found mail: " << mail.identifier();
                // if (!mail.isSent()) {
                    toSend << mail;
                // }
                return true;
            });
            auto job = KAsync::null<void>();
            for (const auto &m : toSend) {
                job = job.then(send(m, mSettings)).then<void>([]() {
                    //on success, mark the mail as sent and move it to a separate place
                    //TODO
                });
            }
            job = job.then<void>([&future]() {
                future.setFinished();
            },
            [&future](int errorCode, const QString &errorString) {
                future.setFinished();
            });
            job.exec();
        });
    }

public:
    QByteArray mResourceInstanceIdentifier;
    MailtransportResource::Settings mSettings;
};

MailtransportResource::MailtransportResource(const QByteArray &instanceIdentifier, const QSharedPointer<Sink::Pipeline> &pipeline)
    : Sink::GenericResource(PLUGIN_NAME, instanceIdentifier, pipeline)
{
    auto config = ResourceConfig::getConfiguration(instanceIdentifier);
    mSettings = {config.value("server").toString(),
                config.value("username").toString(),
                config.value("cacert").toString(),
                config.value("password").toString(),
                config.value("testmode").toBool()
    };

    auto synchronizer = QSharedPointer<MailtransportSynchronizer>::create(PLUGIN_NAME, instanceIdentifier);
    synchronizer->mSettings = mSettings;
    setupSynchronizer(synchronizer);

    auto changereplay = QSharedPointer<MailtransportWriteback>::create(PLUGIN_NAME, instanceIdentifier);
    changereplay->mSettings = mSettings;
    setupChangereplay(changereplay);

    setupPreprocessors(ENTITY_TYPE_MAIL, QVector<Sink::Preprocessor*>() << new MimeMessageMover(mResourceInstanceIdentifier));
}

void MailtransportResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    GenericResource::removeFromDisk(instanceIdentifier);
    Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".synchronization", Sink::Storage::ReadWrite).removeFromDisk();
}

KAsync::Job<void> MailtransportResource::inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
{
    return KAsync::null<void>();
}

MailtransportResourceFactory::MailtransportResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent)
{

}

Sink::Resource *MailtransportResourceFactory::createResource(const QByteArray &instanceIdentifier)
{
    return new MailtransportResource(instanceIdentifier);
}

void MailtransportResourceFactory::registerFacades(Sink::FacadeFactory &factory)
{
    factory.registerFacade<ApplicationDomain::Mail, DefaultFacade<ApplicationDomain::Mail, DomainTypeAdaptorFactory<ApplicationDomain::Mail>>>(PLUGIN_NAME);
}

void MailtransportResourceFactory::registerAdaptorFactories(Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<Sink::ApplicationDomain::Mail, DomainTypeAdaptorFactory<ApplicationDomain::Mail>>(PLUGIN_NAME);
}
