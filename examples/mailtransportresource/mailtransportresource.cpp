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
#include <QUrl>
#include <KMime/Message>

#include "mailtransport.h"
#include "inspection.h"
#include <synchronizer.h>
#include <log.h>
#include <resourceconfig.h>
#include <mailpreprocessor.h>
#include <adaptorfactoryregistry.h>

#define ENTITY_TYPE_MAIL "mail"

using namespace Sink;

struct Settings {
    QString server;
    QString username;
    QString cacert;
    bool testMode;
};

class MailtransportPreprocessor : public Sink::Preprocessor
{
public:
    MailtransportPreprocessor() : Sink::Preprocessor() {}

    QByteArray getTargetResource()
    {
        using namespace Sink::ApplicationDomain;

        auto resource = Store::readOne<ApplicationDomain::SinkResource>(Query{}.filter(resourceInstanceIdentifier()).request<ApplicationDomain::SinkResource::Account>());
        if (resource.identifier().isEmpty()) {
            SinkWarning() << "Failed to retrieve this resource: " << resourceInstanceIdentifier();
        }
        Query query;
        query.containsFilter<ApplicationDomain::SinkResource::Capabilities>(ApplicationDomain::ResourceCapabilities::Mail::sent);
        query.filter<ApplicationDomain::SinkResource::Account>(resource.getAccount());
        auto targetResource = Store::readOne<ApplicationDomain::SinkResource>(query);
        if (targetResource.identifier().isEmpty()) {
            SinkWarning() << "Failed to find target resource: " << targetResource.identifier();
        }
        return targetResource.identifier();
    }

    virtual Result process(Type type, const ApplicationDomain::ApplicationDomainType &current, ApplicationDomain::ApplicationDomainType &diff) Q_DECL_OVERRIDE
    {
        if (type == Preprocessor::Modification) {
            using namespace Sink::ApplicationDomain;
            if (diff.changedProperties().contains(Mail::Trash::name)) {
                //Move back to regular resource
                diff.setResource(getTargetResource());
                return {MoveToResource};
            } else if (diff.changedProperties().contains(Mail::Draft::name)) {
                //Move back to regular resource
                diff.setResource(getTargetResource());
                return {MoveToResource};
            }
        }
        return {NoAction};
    }
};

class MailtransportSynchronizer : public Sink::Synchronizer {
public:
    MailtransportSynchronizer(const Sink::ResourceContext &resourceContext)
        : Sink::Synchronizer(resourceContext),
        mResourceInstanceIdentifier(resourceContext.instanceId())
    {

    }

    KAsync::Job<void> send(const ApplicationDomain::Mail &mail, const Settings &settings)
    {
        return KAsync::start([=] {
            if (!syncStore().readValue(mail.identifier()).isEmpty()) {
                SinkLog() << "Mail is already sent: " << mail.identifier();
                return KAsync::null();
            }
            emitNotification(Notification::Info, ApplicationDomain::SyncInProgress, "Sending message.", {}, {mail.identifier()});
            const auto data = mail.getMimeMessage();
            auto msg = KMime::Message::Ptr::create();
            msg->setContent(KMime::CRLFtoLF(data));
            msg->parse();
            if (settings.testMode) {
                auto subject = msg->subject(true)->asUnicodeString();
                SinkLog() << "I would totally send that mail, but I'm in test mode." << mail.identifier() << subject;
                if (!subject.contains("send")) {
                    return KAsync::error("Failed to send the message.");
                }
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
                    if (settings.server.contains("465")) {
                        options |= MailTransport::UseTls;
                    } else {
                        options |= MailTransport::UseStarttls;
                    }
                }

                SinkLog() << "Sending message " << settings.server << settings.username << "CaCert: " << settings.cacert << "Using tls: " << bool(options & MailTransport::UseTls);
                SinkTrace() << "Sending message " << msg;
                auto result = MailTransport::sendMessage(msg, settings.server.toUtf8(), settings.username.toUtf8(), secret().toUtf8(), settings.cacert.toUtf8(), options);
                if (!result.error) {
                    SinkWarning() << "Failed to send message: " << mail << "\n" << result.errorMessage;
                    const auto errorMessage = QString("Failed to send the message: %1").arg(result.errorMessage);
                    emitNotification(Notification::Warning, ApplicationDomain::SyncError, errorMessage, {}, {mail.identifier()});
                    emitNotification(Notification::Warning, ApplicationDomain::TransmissionError, errorMessage, {}, {mail.identifier()});
                    return KAsync::error(errorMessage.toUtf8().constData());
                } else {
                    emitNotification(Notification::Info, ApplicationDomain::SyncSuccess, "Message successfully sent.", {}, {mail.identifier()});
                    emitNotification(Notification::Info, ApplicationDomain::TransmissionSuccess, "Message successfully sent.", {}, {mail.identifier()});
                }
            }
            syncStore().writeValue(mail.identifier(), "sent");

            SinkLog() << "Sent mail, and triggering move to sent mail folder: " << mail.identifier();
            auto modifiedMail = ApplicationDomain::Mail(mResourceInstanceIdentifier, mail.identifier(), mail.revision(), QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create());
            modifiedMail.setSent(true);

            auto resource = Store::readOne<ApplicationDomain::SinkResource>(Query{}.filter(mResourceInstanceIdentifier).request<ApplicationDomain::SinkResource::Account>());
            if (resource.identifier().isEmpty()) {
                SinkWarning() << "Failed to retrieve target resource: " << mResourceInstanceIdentifier;
            }
            //Then copy the mail to the target resource
            Query query;
            query.containsFilter<ApplicationDomain::SinkResource::Capabilities>(ApplicationDomain::ResourceCapabilities::Mail::sent);
            query.filter<ApplicationDomain::SinkResource::Account>(resource.getAccount());
            return Store::fetchOne<ApplicationDomain::SinkResource>(query)
                .then([this, modifiedMail](const ApplicationDomain::SinkResource &resource) {
                    //Modify the mail to have the sent property set to true, and move it to the new resource.
                    modify(modifiedMail, resource.identifier(), true);
                });
        });
    }

    KAsync::Job<void> synchronizeWithSource(const Sink::QueryBase &query) Q_DECL_OVERRIDE
    {
        if (!QUrl{mSettings.server}.isValid()) {
            return KAsync::error(ApplicationDomain::ConfigurationError, "Invalid server url: " + mSettings.server);
        }
        return KAsync::start<void>([this]() {
            QList<ApplicationDomain::Mail> toSend;
            SinkLog() << "Looking for mails to send.";
            store().readAll<ApplicationDomain::Mail>([&](const ApplicationDomain::Mail &mail) {
                if (!mail.getSent()) {
                    toSend << mail;
                }
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
        return true;
    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Mail &mail, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        if (operation == Sink::Operation_Creation) {
            SinkTrace() << "Dispatching message.";
            return send(mail, mSettings)
                .then(KAsync::value(QByteArray{}));
        } else if (operation == Sink::Operation_Removal) {
            syncStore().removeValue(mail.identifier(), "");
        } else if (operation == Sink::Operation_Modification) {
        }
        return KAsync::null<QByteArray>();
    }

public:
    QByteArray mResourceInstanceIdentifier;
    Settings mSettings;
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

    auto synchronizer = QSharedPointer<MailtransportSynchronizer>::create(resourceContext);
    synchronizer->mSettings = {config.value("server").toString(),
                config.value("username").toString(),
                config.value("cacert").toString(),
                config.value("testmode").toBool()
    };
    setupSynchronizer(synchronizer);
    setupInspector(QSharedPointer<MailtransportInspector>::create(resourceContext));

    setupPreprocessors(ENTITY_TYPE_MAIL, QVector<Sink::Preprocessor*>() << new MailPropertyExtractor << new MailtransportPreprocessor);
}

MailtransportResourceFactory::MailtransportResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent, {Sink::ApplicationDomain::ResourceCapabilities::Mail::mail,
            Sink::ApplicationDomain::ResourceCapabilities::Mail::transport})
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
