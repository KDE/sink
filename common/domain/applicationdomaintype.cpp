/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include "applicationdomaintype.h"
#include "log.h"
#include "../bufferadaptor.h"
#include "definitions.h"
#include "propertyregistry.h"
#include "storage.h" //for generateUid()
#include <QFile>

QDebug Sink::ApplicationDomain::operator<< (QDebug d, const Sink::ApplicationDomain::Mail::Contact &c)
{
    d << "Contact(" << c.name << ", " << c.emailAddress << ")";
    return d;
}

QDebug Sink::ApplicationDomain::operator<< (QDebug d, const Sink::ApplicationDomain::ApplicationDomainType &type)
{
    d << "ApplicationDomainType(\n";
    auto properties = [&] {
        if (!type.changedProperties().isEmpty()) {
            return type.changedProperties();
        } else {
            return type.mAdaptor->availableProperties();
        }
    }();
    std::sort(properties.begin(), properties.end());
    d << " " << "Id: " << "\t" << type.identifier() << "\n";
    d << " " << "Resource: " << "\t" << type.resourceInstanceIdentifier() << "\n";
    for (const auto &property : properties) {
        d << " " << property << "\t" << type.getProperty(property) << "\n";
    }
    d << ")";
    return d;
}

QDebug Sink::ApplicationDomain::operator<< (QDebug d, const Sink::ApplicationDomain::Reference &ref)
{
    d << ref.value;
    return d;
}

QDebug Sink::ApplicationDomain::operator<< (QDebug d, const Sink::ApplicationDomain::BLOB &blob)
{
    d << blob.value << "external:" << blob.isExternal ;
    return d;
}

template <typename DomainType, typename Property>
int registerProperty() {
    Sink::Private::PropertyRegistry::instance().registerProperty<Property>(Sink::ApplicationDomain::getTypeName<DomainType>());
    return 0;
}

#define SINK_REGISTER_ENTITY(ENTITY) \
    constexpr const char *ENTITY::name;

#define SINK_REGISTER_PROPERTY(ENTITYTYPE, PROPERTY) \
    constexpr const char *ENTITYTYPE::PROPERTY::name; \
    static int foo##ENTITYTYPE##PROPERTY = registerProperty<ENTITYTYPE, ENTITYTYPE::PROPERTY>();

namespace Sink {
namespace ApplicationDomain {

constexpr const char *SinkResource::name;
constexpr const char *SinkAccount::name;

SINK_REGISTER_ENTITY(Mail);
SINK_REGISTER_PROPERTY(Mail, Sender);
SINK_REGISTER_PROPERTY(Mail, To);
SINK_REGISTER_PROPERTY(Mail, Cc);
SINK_REGISTER_PROPERTY(Mail, Bcc);
SINK_REGISTER_PROPERTY(Mail, Subject);
SINK_REGISTER_PROPERTY(Mail, Date);
SINK_REGISTER_PROPERTY(Mail, Unread);
SINK_REGISTER_PROPERTY(Mail, Important);
SINK_REGISTER_PROPERTY(Mail, Folder);
SINK_REGISTER_PROPERTY(Mail, MimeMessage);
SINK_REGISTER_PROPERTY(Mail, FullPayloadAvailable);
SINK_REGISTER_PROPERTY(Mail, Draft);
SINK_REGISTER_PROPERTY(Mail, Trash);
SINK_REGISTER_PROPERTY(Mail, Sent);
SINK_REGISTER_PROPERTY(Mail, MessageId);
SINK_REGISTER_PROPERTY(Mail, ParentMessageId);
SINK_REGISTER_PROPERTY(Mail, ThreadId);

SINK_REGISTER_ENTITY(Folder);
SINK_REGISTER_PROPERTY(Folder, Name);
SINK_REGISTER_PROPERTY(Folder, Icon);
SINK_REGISTER_PROPERTY(Folder, SpecialPurpose);
SINK_REGISTER_PROPERTY(Folder, Enabled);
SINK_REGISTER_PROPERTY(Folder, Parent);
SINK_REGISTER_PROPERTY(Folder, Count);
SINK_REGISTER_PROPERTY(Folder, FullContentAvailable);

SINK_REGISTER_ENTITY(Contact);
SINK_REGISTER_PROPERTY(Contact, Uid);
SINK_REGISTER_PROPERTY(Contact, Fn);
SINK_REGISTER_PROPERTY(Contact, Firstname);
SINK_REGISTER_PROPERTY(Contact, Lastname);
SINK_REGISTER_PROPERTY(Contact, Emails);
SINK_REGISTER_PROPERTY(Contact, Vcard);
SINK_REGISTER_PROPERTY(Contact, Addressbook);
SINK_REGISTER_PROPERTY(Contact, Photo);

SINK_REGISTER_ENTITY(Addressbook);
SINK_REGISTER_PROPERTY(Addressbook, Name);
SINK_REGISTER_PROPERTY(Addressbook, Parent);
SINK_REGISTER_PROPERTY(Addressbook, LastUpdated);

static const int foo = [] {
    QMetaType::registerEqualsComparator<Reference>();
    QMetaType::registerDebugStreamOperator<Reference>();
    QMetaType::registerConverter<Reference, QByteArray>();
    QMetaType::registerDebugStreamOperator<BLOB>();
    QMetaType::registerDebugStreamOperator<Mail::Contact>();
    qRegisterMetaTypeStreamOperators<Sink::ApplicationDomain::Reference>();
    return 0;
}();

void copyBuffer(Sink::ApplicationDomain::BufferAdaptor &buffer, Sink::ApplicationDomain::BufferAdaptor &memoryAdaptor, const QList<QByteArray> &properties, bool copyBlobs, bool pruneReferences)
{
    auto propertiesToCopy = properties;
    if (properties.isEmpty()) {
        propertiesToCopy = buffer.availableProperties();
    }
    for (const auto &property : propertiesToCopy) {
        const auto value = buffer.getProperty(property);
        if (copyBlobs && value.canConvert<BLOB>()) {
            const auto oldPath = value.value<BLOB>().value;
            const auto newPath = Sink::temporaryFileLocation() + "/" + QUuid::createUuid().toString();
            QFile::copy(oldPath, newPath);
            memoryAdaptor.setProperty(property, QVariant::fromValue(BLOB{newPath}));
        } else if (pruneReferences && value.canConvert<Reference>()) {
            continue;
        } else {
            memoryAdaptor.setProperty(property, value);
        }
    }
}

ApplicationDomainType::ApplicationDomainType()
    :mAdaptor(new MemoryBufferAdaptor()),
    mChangeSet(new QSet<QByteArray>())
{

}

ApplicationDomainType::ApplicationDomainType(const QByteArray &resourceInstanceIdentifier)
    :mAdaptor(new MemoryBufferAdaptor()),
    mChangeSet(new QSet<QByteArray>()),
    mResourceInstanceIdentifier(resourceInstanceIdentifier)
{

}

ApplicationDomainType::ApplicationDomainType(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor)
    : mAdaptor(adaptor),
    mChangeSet(new QSet<QByteArray>()),
    mResourceInstanceIdentifier(resourceInstanceIdentifier),
    mIdentifier(identifier),
    mRevision(revision)
{
}

ApplicationDomainType::ApplicationDomainType(const ApplicationDomainType &other)
    : mChangeSet(new QSet<QByteArray>())
{
    *this = other;
}

ApplicationDomainType& ApplicationDomainType::operator=(const ApplicationDomainType &other)
{
    mAdaptor = other.mAdaptor;
    if (other.mChangeSet) {
        *mChangeSet = *other.mChangeSet;
    }
    mResourceInstanceIdentifier = other.mResourceInstanceIdentifier;
    mIdentifier = other.mIdentifier;
    mRevision = other.mRevision;
    return *this;
}

ApplicationDomainType::~ApplicationDomainType()
{
}

QByteArray ApplicationDomainType::generateUid()
{
    return Sink::Storage::DataStore::generateUid();
}

bool ApplicationDomainType::hasProperty(const QByteArray &key) const
{
    Q_ASSERT(mAdaptor);
    return mAdaptor->availableProperties().contains(key);
}

QVariant ApplicationDomainType::getProperty(const QByteArray &key) const
{
    Q_ASSERT(mAdaptor);
    return mAdaptor->getProperty(key);
}

void ApplicationDomainType::setProperty(const QByteArray &key, const QVariant &value)
{
    Q_ASSERT(mAdaptor);
    auto existing = mAdaptor->getProperty(key);
    if (existing.isValid() && existing == value) {
        SinkTrace() << "Tried to set property that is still the same: " << key << value;
    } else {
        mChangeSet->insert(key);
        mAdaptor->setProperty(key, value);
    }
}

void ApplicationDomainType::setResource(const QByteArray &identifier)
{
    mResourceInstanceIdentifier = identifier;
}

void ApplicationDomainType::setProperty(const QByteArray &key, const ApplicationDomainType &value)
{
    Q_ASSERT(!value.identifier().isEmpty());
    setProperty(key, QVariant::fromValue(Reference{value.identifier()}));
}

QByteArray ApplicationDomainType::getBlobProperty(const QByteArray &key) const
{
    const auto path = getProperty(key).value<BLOB>().value;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        SinkError() << "Failed to open the file for reading: " << file.errorString() << "Path:" << path << " For property:" << key;
        return QByteArray();
    }
    return file.readAll();
}

void ApplicationDomainType::setBlobProperty(const QByteArray &key, const QByteArray &value)
{
    const auto path = Sink::temporaryFileLocation() + "/" + QUuid::createUuid().toString();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        SinkError() << "Failed to open the file for writing: " << file.errorString() << path<< " For property " << key;
        return;
    }
    file.write(value);
    //Ensure that the file is written to disk immediately
    file.close();
    setProperty(key, QVariant::fromValue(BLOB{path}));
}

void ApplicationDomainType::setChangedProperties(const QSet<QByteArray> &changeset)
{
    *mChangeSet = changeset;
}

QByteArrayList ApplicationDomainType::changedProperties() const
{
    return mChangeSet->toList();
}

QByteArrayList ApplicationDomainType::availableProperties() const
{
    Q_ASSERT(mAdaptor);
    return mAdaptor->availableProperties();
}

qint64 ApplicationDomainType::revision() const
{
    return mRevision;
}

QByteArray ApplicationDomainType::resourceInstanceIdentifier() const
{
    return mResourceInstanceIdentifier;
}

QByteArray ApplicationDomainType::identifier() const
{
    return mIdentifier;
}

SinkResource::SinkResource(const QByteArray &identifier)
    : ApplicationDomainType("", identifier, 0, QSharedPointer<BufferAdaptor>(new MemoryBufferAdaptor()))
{

}

SinkResource::SinkResource(const QByteArray &, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor)
    : ApplicationDomainType("", identifier, 0, adaptor)
{
}

SinkResource::SinkResource()
    : ApplicationDomainType()
{

}

SinkResource::~SinkResource()
{

}

SinkAccount::SinkAccount(const QByteArray &identifier)
    : ApplicationDomainType("", identifier, 0, QSharedPointer<BufferAdaptor>(new MemoryBufferAdaptor()))
{

}

SinkAccount::SinkAccount(const QByteArray &, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor)
    : ApplicationDomainType("", identifier, 0, adaptor)
{
}

SinkAccount::SinkAccount()
    : ApplicationDomainType()
{

}

SinkAccount::~SinkAccount()
{

}

Identity::Identity(const QByteArray &identifier)
    : ApplicationDomainType("", identifier, 0, QSharedPointer<BufferAdaptor>(new MemoryBufferAdaptor()))
{

}

Identity::Identity(const QByteArray &, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor)
    : ApplicationDomainType("", identifier, 0, adaptor)
{
}

Identity::Identity()
    : ApplicationDomainType()
{

}

Identity::~Identity()
{

}

SinkResource DummyResource::create(const QByteArray &account)
{
    auto &&resource = ApplicationDomainType::createEntity<SinkResource>();
    resource.setResourceType("sink.dummy");
    resource.setAccount(account);
    return resource;
}

SinkResource MaildirResource::create(const QByteArray &account)
{
    auto &&resource = ApplicationDomainType::createEntity<SinkResource>();
    resource.setResourceType("sink.maildir");
    resource.setAccount(account);
    return resource;
}

SinkResource MailtransportResource::create(const QByteArray &account)
{
    auto &&resource = ApplicationDomainType::createEntity<SinkResource>();
    resource.setResourceType("sink.mailtransport");
    resource.setAccount(account);
    return resource;
}

SinkResource ImapResource::create(const QByteArray &account)
{
    auto &&resource = ApplicationDomainType::createEntity<SinkResource>();
    resource.setResourceType("sink.imap");
    resource.setAccount(account);
    return resource;
}

SinkResource CardDavResource::create(const QByteArray &account)
{
    auto &&resource = ApplicationDomainType::createEntity<SinkResource>();
    resource.setResourceType("sink.dav");
    resource.setAccount(account);
    return resource;
}

QByteArrayList getTypeNames()
{
    static QByteArrayList types;
    if (types.isEmpty()) {
#define REGISTER_TYPE(TYPE) \
        types << ApplicationDomain::getTypeName<TYPE>();
SINK_REGISTER_TYPES()
#undef REGISTER_TYPE
    }
    return types;
}

bool isGlobalType(const QByteArray &type) {
    if (type == ApplicationDomain::getTypeName<SinkResource>() || type == ApplicationDomain::getTypeName<SinkAccount>() || type == ApplicationDomain::getTypeName<Identity>()) {
        return true;
    }
    return false;
}

}
}

QDataStream &operator<<(QDataStream &out, const Sink::ApplicationDomain::Reference &reference)
{
    out << reference.value;
    return out;
}

QDataStream &operator>>(QDataStream &in, Sink::ApplicationDomain::Reference &reference)
{
    in >> reference.value;
    return in;
}

