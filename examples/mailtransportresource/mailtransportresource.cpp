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
#include "inspector.h"
#include "store.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <KMime/Message>

#include "mailtransport.h"
#include "mail_generated.h"
#include "inspection.h"
#include <synchronizer.h>
#include <log.h>
#include <resourceconfig.h>
#include <mailpreprocessor.h>
#include <adaptorfactoryregistry.h>

#define ENTITY_TYPE_MAIL "mail"

SINK_DEBUG_AREA("mailtransportresource")

using namespace Sink;

class MailtransportSynchronizer : public Sink::Synchronizer {
public:
    MailtransportSynchronizer(const Sink::ResourceContext &resourceContext)
        : Sink::Synchronizer(resourceContext),
        mResourceInstanceIdentifier(resourceContext.instanceId())
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
            MailTransport::Options options;
            if (settings.server.contains("smtps")) {
                options &= MailTransport::UseTls;
            }
            if (MailTransport::sendMessage(msg, settings.server.toUtf8(), settings.username.toUtf8(), settings.password.toUtf8(), settings.cacert.toUtf8(), options)) {
                SinkLog() << "Sent message successfully";
            } else {
                SinkLog() << "Failed to send message";
                return KAsync::error<void>(1, "Failed to send the message.");
            }
        }
        return KAsync::start<void>([=] {
            SinkLog() << "Sent mail, and triggering move to sent mail folder: " << mail.identifier();
            auto modifiedMail = ApplicationDomain::Mail(mResourceInstanceIdentifier, mail.identifier(), mail.revision(), QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
            modifiedMail.setSent(true);

            auto resource = Store::readOne<ApplicationDomain::SinkResource>(Query{}.filter(mResourceInstanceIdentifier).request<ApplicationDomain::SinkResource::Account>());
            //Then copy the mail to the target resource
            Query query;
            query.containsFilter<ApplicationDomain::SinkResource::Capabilities>(ApplicationDomain::ResourceCapabilities::Mail::sent);
            query.filter<ApplicationDomain::SinkResource::Account>(resource.getAccount());
            return Store::fetchOne<ApplicationDomain::SinkResource>(query)
                .then([this, modifiedMail](const ApplicationDomain::SinkResource &resource) {
                    //First modify the mail to have the sent property set to true
                    modify(modifiedMail, resource.identifier(), true);
                    return KAsync::null<void>();
                });
        });
    }

    KAsync::Job<void> synchronizeWithSource(const Sink::QueryBase &query) Q_DECL_OVERRIDE
    {
        return KAsync::start<void>([this]() {
            QList<ApplicationDomain::Mail> toSend;
            SinkLog() << "Looking for mails to send.";
            store().readAll<ApplicationDomain::Mail>([&](const ApplicationDomain::Mail &mail) -> bool {
                SinkTrace() << "Found mail: " << mail.identifier();
                if (!mail.getSent()) {
                    toSend << mail;
                }
                return true;
            });
            SinkLog() << "Found " << toSend.size() << " mails to send";
            auto job = KAsync::null<void>();
            for (const auto &m : toSend) {
                job = job.then(send(m, mSettings));
            }
            return job;
        });
    }

    bool canReplay(const QByteArray &type, const QByteArray &key, const QByteArray &value) Q_DECL_OVERRIDE
    {
        return false;
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
    QByteArray mResourceInstanceIdentifier;
    MailtransportResource::Settings mSettings;
};

class MailtransportInspector : public Sink::Inspector {
public:
    MailtransportInspector(const Sink::ResourceContext &resourceContext)
        : Sink::Inspector(resourceContext)
    {

    }

protected:
    KAsync::Job<void> inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue) Q_DECL_OVERRIDE
    {
        if (domainType == ENTITY_TYPE_MAIL) {
            if (inspectionType == Sink::ResourceControl::Inspection::ExistenceInspectionType) {
                auto path = resourceStorageLocation(mResourceContext.instanceId()) + "/test/" + entityId;
                if (QFileInfo::exists(path)) {
                    return KAsync::null<void>();
                }
                return KAsync::error<void>(1, "Couldn't find message: " + path);
            }
        }
        return KAsync::null<void>();
    }
};


MailtransportResource::MailtransportResource(const Sink::ResourceContext &resourceContext)
    : Sink::GenericResource(resourceContext)
{
    auto config = ResourceConfig::getConfiguration(resourceContext.instanceId());
    mSettings = {config.value("server").toString(),
                config.value("username").toString(),
                config.value("cacert").toString(),
                config.value("password").toString(),
                config.value("testmode").toBool()
    };

    auto synchronizer = QSharedPointer<MailtransportSynchronizer>::create(resourceContext);
    synchronizer->mSettings = mSettings;
    setupSynchronizer(synchronizer);
    setupInspector(QSharedPointer<MailtransportInspector>::create(resourceContext));

    setupPreprocessors(ENTITY_TYPE_MAIL, QVector<Sink::Preprocessor*>() << new MailPropertyExtractor);
}

MailtransportResourceFactory::MailtransportResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent, {Sink::ApplicationDomain::ResourceCapabilities::Mail::transport})
{

}

Sink::Resource *MailtransportResourceFactory::createResource(const Sink::ResourceContext &context)
{
    return new MailtransportResource(context);
}

void MailtransportResourceFactory::registerFacades(const QByteArray &resourceName, Sink::FacadeFactory &factory)
{
    factory.registerFacade<ApplicationDomain::Mail, DefaultFacade<ApplicationDomain::Mail>>(resourceName);
}

void MailtransportResourceFactory::registerAdaptorFactories(const QByteArray &resourceName, Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<Sink::ApplicationDomain::Mail, DomainTypeAdaptorFactory<ApplicationDomain::Mail>>(resourceName);
}

void MailtransportResourceFactory::removeDataFromDisk(const QByteArray &instanceIdentifier)
{
    MailtransportResource::removeFromDisk(instanceIdentifier);
}
