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

#include "carddavresource.h"

#include "../webdavcommon/webdav.h"

#include "facade.h"
#include "resourceconfig.h"
#include "log.h"
#include "definitions.h"
#include "synchronizer.h"
#include "inspector.h"

#include "facadefactory.h"
#include "adaptorfactoryregistry.h"

#include "contactpreprocessor.h"

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_CONTACT "contact"
#define ENTITY_TYPE_ADDRESSBOOK "addressbook"

using namespace Sink;

class ContactSynchronizer : public WebDavSynchronizer
{
public:
    ContactSynchronizer(const Sink::ResourceContext &resourceContext)
        : WebDavSynchronizer(resourceContext, KDAV2::CardDav,
              ApplicationDomain::getTypeName<ApplicationDomain::Addressbook>(),
              {ApplicationDomain::getTypeName<ApplicationDomain::Contact>()})
    {}
    QByteArray createAddressbook(const QString &addressbookName, const QString &addressbookPath, const QString &parentAddressbookRid)
    {
        SinkTrace() << "Creating addressbook: " << addressbookName << parentAddressbookRid;
        const auto remoteId = addressbookPath.toUtf8();
        const auto bufferType = ENTITY_TYPE_ADDRESSBOOK;
        Sink::ApplicationDomain::Addressbook addressbook;
        addressbook.setName(addressbookName);
        addressbook.setEnabled(true);

        if (!parentAddressbookRid.isEmpty()) {
            addressbook.setParent(syncStore().resolveRemoteId(ENTITY_TYPE_ADDRESSBOOK, parentAddressbookRid.toUtf8()));
        }
        createOrModify(bufferType, remoteId, addressbook, {});
        return remoteId;
    }

protected:
    void updateLocalCollections(KDAV2::DavCollection::List addressbookList) Q_DECL_OVERRIDE
    {
        const QByteArray bufferType = ENTITY_TYPE_ADDRESSBOOK;
        SinkTrace() << "Found" << addressbookList.size() << "addressbooks";

        for (const auto &f : addressbookList) {
            const auto &rid = resourceID(f);
            SinkLog() << "Found addressbook:" << rid << f.displayName();
            createAddressbook(f.displayName(), rid, "");
        }
    }

    void updateLocalItem(KDAV2::DavItem remoteContact, const QByteArray &addressbookLocalId) Q_DECL_OVERRIDE
    {
        Sink::ApplicationDomain::Contact localContact;

        localContact.setVcard(remoteContact.data());
        localContact.setAddressbook(addressbookLocalId);

        createOrModify(ENTITY_TYPE_CONTACT, resourceID(remoteContact), localContact, {});
    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Contact &contact, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        SinkLog() << "Replaying to:" << serverUrl().url();
        switch (operation) {
            case Sink::Operation_Creation: {
                const auto vcard = contact.getVcard();
                if (vcard.isEmpty()) {
                    return KAsync::error<QByteArray>("No vcard in item for creation replay.");
                }
                return createItem(vcard, "text/vcard", contact.getUid().toUtf8(), syncStore().resolveLocalId(ENTITY_TYPE_ADDRESSBOOK, contact.getAddressbook()));
            }
            case Sink::Operation_Removal: {
                return removeItem(oldRemoteId);
            }
            case Sink::Operation_Modification:
                const auto vcard = contact.getVcard();
                if (vcard.isEmpty()) {
                    return KAsync::error<QByteArray>("No ICal in item for modification replay");
                }
                return modifyItem(oldRemoteId, vcard, "text/vcard", syncStore().resolveLocalId(ENTITY_TYPE_ADDRESSBOOK, contact.getAddressbook()));
        }
        return KAsync::null<QByteArray>();
    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Addressbook &addressbook, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        return KAsync::null<QByteArray>();
    }
};

class CollectionCleanupPreprocessor : public Sink::Preprocessor
{
public:
    virtual void deletedEntity(const ApplicationDomain::ApplicationDomainType &oldEntity) Q_DECL_OVERRIDE
    {
        //Remove all events of a collection when removing the collection.
        const auto revision = entityStore().maxRevision();
        entityStore().indexLookup<ApplicationDomain::Contact, ApplicationDomain::Contact::Addressbook>(oldEntity.identifier(), [&] (const QByteArray &identifier) {
            deleteEntity(ApplicationDomain::ApplicationDomainType{{}, identifier, revision, {}}, ApplicationDomain::getTypeName<ApplicationDomain::Contact>(), false);
        });
    }
};

CardDavResource::CardDavResource(const Sink::ResourceContext &resourceContext)
    : Sink::GenericResource(resourceContext)
{
    auto synchronizer = QSharedPointer<ContactSynchronizer>::create(resourceContext);
    setupSynchronizer(synchronizer);

    setupPreprocessors(ENTITY_TYPE_CONTACT, {new ContactPropertyExtractor});
    setupPreprocessors(ENTITY_TYPE_ADDRESSBOOK, {new CollectionCleanupPreprocessor});
}


CardDavResourceFactory::CardDavResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent,
            {Sink::ApplicationDomain::ResourceCapabilities::Contact::contact,
            Sink::ApplicationDomain::ResourceCapabilities::Contact::addressbook,
            Sink::ApplicationDomain::ResourceCapabilities::Contact::storage
            }
            )
{
}

Sink::Resource *CardDavResourceFactory::createResource(const ResourceContext &context)
{
    return new CardDavResource(context);
}

void CardDavResourceFactory::registerFacades(const QByteArray &name, Sink::FacadeFactory &factory)
{
    factory.registerFacade<ApplicationDomain::Contact, DefaultFacade<ApplicationDomain::Contact>>(name);
    factory.registerFacade<ApplicationDomain::Addressbook, DefaultFacade<ApplicationDomain::Addressbook>>(name);
}

void CardDavResourceFactory::registerAdaptorFactories(const QByteArray &name, Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<ApplicationDomain::Contact, DefaultAdaptorFactory<ApplicationDomain::Contact>>(name);
    registry.registerFactory<ApplicationDomain::Addressbook, DefaultAdaptorFactory<ApplicationDomain::Addressbook>>(name);
}

void CardDavResourceFactory::removeDataFromDisk(const QByteArray &instanceIdentifier)
{
    CardDavResource::removeFromDisk(instanceIdentifier);
}
