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
#include "inspection.h"
#include <synchronizer.h>
#include <log.h>
#include <resourceconfig.h>
#include <pipeline.h>
#include <mailpreprocessor.h>
#include <indexupdater.h>

#define ENTITY_TYPE_MAIL "mail"

SINK_DEBUG_AREA("mailtransportresource")

using namespace Sink;

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
            SinkTrace() << "Dispatching message.";
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
        : Sink::Synchronizer(resourceType, resourceInstanceIdentifier),
        mResourceInstanceIdentifier(resourceInstanceIdentifier)
    {

    }

    KAsync::Job<void>send(const ApplicationDomain::Mail &mail, const MailtransportResource::Settings &settings)
    {
        const auto data = mail.getMimeMessage();
        auto msg = KMime::Message::Ptr::create();
        msg->setHead(KMime::CRLFtoLF(data));
        msg->parse();
        if (settings.testMode) {
            SinkLog() << "I would totally send that mail, but I'm in test mode." << mail.identifier();
            auto path = resourceStorageLocation(mResourceInstanceIdentifier) + "/test/";
            SinkTrace() << path;
            QDir dir;
            dir.mkpath(path);
            QFile f(path+ mail.identifier());
            f.open(QIODevice::ReadWrite);
            f.write("foo");
            f.close();
        } else {
            if (MailTransport::sendMessage(msg, settings.server.toUtf8(), settings.username.toUtf8(), settings.password.toUtf8(), settings.cacert.toUtf8())) {
                SinkLog() << "Sent message successfully";
            } else {
                SinkLog() << "Failed to send message";
                return KAsync::error<void>(1, "Failed to send the message.");
            }
        }
        return KAsync::null<void>();
    }

    KAsync::Job<void> synchronizeWithSource() Q_DECL_OVERRIDE
    {
        SinkLog() << " Synchronizing";
        return KAsync::start<void>([this](KAsync::Future<void> future) {
            Sink::Query query;
            QList<ApplicationDomain::Mail> toSend;
            SinkLog() << " Looking for mail";
            store().reader<ApplicationDomain::Mail>().query(query, [&](const ApplicationDomain::Mail &mail) -> bool {
                SinkTrace() << "Found mail: " << mail.identifier();
                if (!mail.getSent()) {
                    toSend << mail;
                }
                return true;
            });
            auto job = KAsync::null<void>();
            for (const auto &m : toSend) {
                job = job.then(send(m, mSettings)).then<void>([this, m]() {
                    auto modifiedMail = ApplicationDomain::Mail(mResourceInstanceIdentifier, m.identifier(), m.revision(), QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
                    modifiedMail.setSent(true);
                    modify(modifiedMail);
                    //TODO copy to a sent mail folder as well
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

    setupPreprocessors(ENTITY_TYPE_MAIL, QVector<Sink::Preprocessor*>() << new MimeMessageMover << new MailPropertyExtractor << new DefaultIndexUpdater<Sink::ApplicationDomain::Mail>);
}

void MailtransportResource::removeFromDisk(const QByteArray &instanceIdentifier)
{
    GenericResource::removeFromDisk(instanceIdentifier);
    Sink::Storage(Sink::storageLocation(), instanceIdentifier + ".synchronization", Sink::Storage::ReadWrite).removeFromDisk();
}

KAsync::Job<void> MailtransportResource::inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue)
{
    if (domainType == ENTITY_TYPE_MAIL) {
        if (inspectionType == Sink::ResourceControl::Inspection::ExistenceInspectionType) {
            auto path = resourceStorageLocation(mResourceInstanceIdentifier) + "/test/" + entityId;
            if (QFileInfo::exists(path)) {
                return KAsync::null<void>();
            }
            return KAsync::error<void>(1, "Couldn't find message: " + path);
        }
    }
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
