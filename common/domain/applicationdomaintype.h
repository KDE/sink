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
#pragma once

#include "sink_export.h"
#include <QSharedPointer>
#include <QVariant>
#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include "bufferadaptor.h"

#define SINK_ENTITY(TYPE, LOWERCASENAME) \
    static constexpr const char *name = #LOWERCASENAME; \
    typedef QSharedPointer<TYPE> Ptr; \
    using Entity::Entity; \
    TYPE() = default; \
    TYPE(const ApplicationDomainType &o) : Entity(o) {} \
    virtual ~TYPE() = default; \
    static TYPE create(const QByteArray &resource) { return createEntity<TYPE>(resource); }; \


#define SINK_PROPERTY(TYPE, NAME, LOWERCASENAME) \
    struct NAME { \
        static constexpr const char *name = #LOWERCASENAME; \
        typedef TYPE Type; \
    }; \
    void set##NAME(const TYPE &value) { setProperty(NAME::name, QVariant::fromValue(value)); } \
    TYPE get##NAME() const { return getProperty(NAME::name).value<TYPE>(); } \


#define SINK_EXTRACTED_PROPERTY(TYPE, NAME, LOWERCASENAME) \
    struct NAME { \
        static constexpr const char *name = #LOWERCASENAME; \
        typedef TYPE Type; \
    }; \
    void setExtracted##NAME(const TYPE &value) { setProperty(NAME::name, QVariant::fromValue(value)); } \
    TYPE get##NAME() const { return getProperty(NAME::name).value<TYPE>(); } \

#define SINK_STATUS_PROPERTY(TYPE, NAME, LOWERCASENAME) \
    struct NAME { \
        static constexpr const char *name = #LOWERCASENAME; \
        typedef TYPE Type; \
    }; \
    void setStatus##NAME(const TYPE &value) { setProperty(NAME::name, QVariant::fromValue(value)); } \
    TYPE get##NAME() const { return getProperty(NAME::name).value<TYPE>(); } \

#define SINK_REFERENCE_PROPERTY(TYPE, NAME, LOWERCASENAME) \
    struct NAME { \
        static constexpr const char *name = #LOWERCASENAME; \
        typedef Reference Type; \
        typedef ApplicationDomain::TYPE ReferenceType; \
    }; \
    void set##NAME(const ApplicationDomain::TYPE &value) { setProperty(NAME::name, value); } \
    void set##NAME(const QByteArray &value) { setProperty(NAME::name, QVariant::fromValue(Reference{value})); } \
    QByteArray get##NAME() const { return getProperty(NAME::name).value<Reference>().value; } \

#define SINK_INDEX_PROPERTY(TYPE, NAME, LOWERCASENAME) \
    struct NAME { \
        static constexpr const char *name = #LOWERCASENAME; \
        typedef TYPE Type; \
    }; \


namespace Sink {
namespace ApplicationDomain {

enum SINK_EXPORT ErrorCode {
    NoError = 0,
    UnknownError,
    NoServerError,
    ConnectionError,
    LoginError,
    ConfigurationError,
    TransmissionError,
    ConnectionLostError,
    MissingCredentialsError,
    ResourceCrashedError
};

enum SINK_EXPORT SuccessCode {
    TransmissionSuccess
};

enum SINK_EXPORT SyncStatus {
    NoSyncStatus,
    SyncInProgress,
    SyncError,
    SyncSuccess
};

/**
 * The status of an account or resource.
 *
 * It is set as follows:
 * * By default the status is no status.
 * * If a connection to the server failed the status is Offline.
 * * If a connection to the server could be established the status is Connected.
 * * If an error occurred that keeps the resource from operating (so non transient), the resource enters the error state.
 * * If a long running operation is started the resource goes to the busy state (and return to the previous state after that).
 */
enum SINK_EXPORT Status {
    NoStatus,
    OfflineStatus,
    ConnectedStatus,
    BusyStatus,
    ErrorStatus
};

struct SINK_EXPORT Error {

};

struct SINK_EXPORT Progress {

};

/**
 * Internal type.
 *
 * Represents a reference to another entity in the same resource.
 */
struct Reference {
    Reference() = default;
    Reference(const Reference &) = default;
    Reference(const QByteArray &id) : value(id) {};
    Reference(const char *id) : value(id) {};
    ~Reference() = default;
    bool operator==(const Reference &other) const {
        return value == other.value;
    }
    operator QByteArray() const {
        return value;
    }
    QByteArray value;
};

void copyBuffer(Sink::ApplicationDomain::BufferAdaptor &buffer, Sink::ApplicationDomain::BufferAdaptor &memoryAdaptor, const QList<QByteArray> &properties = QList<QByteArray>(), bool pruneReferences = false);

/**
 * The domain type interface has two purposes:
 * * provide a unified interface to read buffers (for zero-copy reading)
 * * record changes to generate changesets for modifications
 *
 * ApplicationDomainTypes don't adhere to any standard and are meant to be extended frequently (hence the non-typesafe interface).
 */
class SINK_EXPORT ApplicationDomainType {
public:
    typedef QSharedPointer<ApplicationDomainType> Ptr;

    ApplicationDomainType();
    explicit ApplicationDomainType(const QByteArray &resourceInstanceIdentifier);
    explicit ApplicationDomainType(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor);
    ApplicationDomainType(const ApplicationDomainType &other);
    ApplicationDomainType& operator=(const ApplicationDomainType &other);

    inline bool operator==(const ApplicationDomainType &other)
    {
        return other.identifier() == identifier();
    }

    inline bool operator!=(const ApplicationDomainType &other)
    {
        return !(*this == other);
    }

    template <typename DomainType>
    DomainType cast() {
        static_assert(std::is_base_of<ApplicationDomainType, DomainType>::value, "You can only cast to base classes of ApplicationDomainType.");
        DomainType t = *this;
        t.mChangeSet = mChangeSet;
        return t;
    }

    /**
     * Returns an in memory representation of the same entity.
     */
    template <typename DomainType>
    static typename DomainType::Ptr getInMemoryRepresentation(const ApplicationDomainType &domainType, const QList<QByteArray> properties = QList<QByteArray>())
    {
        auto memoryAdaptor = QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create();
        copyBuffer(*(domainType.mAdaptor), *memoryAdaptor, properties, false);
        //mIdentifier internally still refers to the memory-mapped memory, we need to copy the memory or it will become invalid
        return QSharedPointer<DomainType>::create(domainType.mResourceInstanceIdentifier, QByteArray(domainType.mIdentifier.constData(), domainType.mIdentifier.size()), domainType.mRevision, memoryAdaptor);
    }

    /**
     * Returns an in memory copy without id and resource set.
     */
    template <typename DomainType>
    static typename DomainType::Ptr getInMemoryCopy(const ApplicationDomainType &domainType, const QList<QByteArray> properties = QList<QByteArray>())
    {
        auto memoryAdaptor = QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create();
        Q_ASSERT(domainType.mAdaptor);
        copyBuffer(*(domainType.mAdaptor), *memoryAdaptor, properties, true);
        return QSharedPointer<DomainType>::create(QByteArray{}, QByteArray{}, 0, memoryAdaptor);
    }

    static QByteArray generateUid();

    template <class DomainType>
    static DomainType createEntity()
    {
        DomainType object;
        object.mIdentifier = generateUid();
        return object;
    }

    template <class DomainType>
    static DomainType createEntity(const QByteArray &resourceInstanceIdentifier)
    {
        DomainType object(resourceInstanceIdentifier);
        object.mIdentifier = generateUid();
        return object;
    }

    template <class DomainType>
    static DomainType createEntity(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier)
    {
        if (identifier.isEmpty()) {
            return createEntity<DomainType>(resourceInstanceIdentifier);
        }
        DomainType object(resourceInstanceIdentifier);
        object.mIdentifier = identifier;
        return object;
    }

    template <class DomainType>
    static DomainType createCopy(const QByteArray &identifier, const DomainType &original)
    {
        DomainType object(original);
        object.mIdentifier = identifier;
        return object;
    }

    virtual ~ApplicationDomainType();

    bool hasProperty(const QByteArray &key) const;

    QVariant getProperty(const QByteArray &key) const;

    /**
     * Set a property and record a changed property
     *
     * If the propery is available and did not change the call will be ignored.
     */
    void setProperty(const QByteArray &key, const QVariant &value);

    /**
     * Convenience method to set a reference property.
     */
    void setProperty(const QByteArray &key, const ApplicationDomainType &value);

    void setChangedProperties(const QSet<QByteArray> &changeset);
    QByteArrayList changedProperties() const;
    QByteArrayList availableProperties() const;
    qint64 revision() const;
    QByteArray resourceInstanceIdentifier() const;
    void setResource(const QByteArray &identifier);
    QByteArray identifier() const;

    bool isAggregate() const;
    QVector<QByteArray> aggregatedIds() const;
    QVector<QByteArray> &aggregatedIds();

private:
    friend QDebug operator<<(QDebug, const ApplicationDomainType &);
    QSharedPointer<BufferAdaptor> mAdaptor;
    QSharedPointer<QSet<QByteArray>> mChangeSet;
    /*
     * Each domain object needs to store the resource, identifier, revision triple so we can link back to the storage location.
     */
    QByteArray mResourceInstanceIdentifier;
    QByteArray mIdentifier;
    qint64 mRevision;

    QVector<QByteArray> mAggreatedIds;
};

/*
 * Should this be specific to the synclistresultset, in other cases we may want to take revision and resource into account.
 */
inline bool operator==(const ApplicationDomainType& lhs, const ApplicationDomainType& rhs)
{
    return lhs.identifier() == rhs.identifier()
            && lhs.resourceInstanceIdentifier() == rhs.resourceInstanceIdentifier();
}

SINK_EXPORT QDebug operator<< (QDebug d, const ApplicationDomainType &type);
SINK_EXPORT QDebug operator<< (QDebug d, const Reference &ref);


struct SINK_EXPORT SinkAccount : public ApplicationDomainType {
    static constexpr const char *name = "account";
    typedef QSharedPointer<SinkAccount> Ptr;
    explicit SinkAccount(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor);
    explicit SinkAccount(const QByteArray &identifier);
    SinkAccount();
    virtual ~SinkAccount();

    SINK_PROPERTY(QString, Name, name);
    SINK_PROPERTY(QString, Icon, icon);
    SINK_PROPERTY(QString, AccountType, type);
    SINK_STATUS_PROPERTY(int, Status, status);
};


/**
 * Represents an sink resource.
 *
 * This type is used for configuration of resources,
 * and for creating and removing resource instances.
 */
struct SINK_EXPORT SinkResource : public ApplicationDomainType {
    static constexpr const char *name = "resource";
    typedef QSharedPointer<SinkResource> Ptr;
    explicit SinkResource(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor);
    explicit SinkResource(const QByteArray &identifier);
    SinkResource();
    virtual ~SinkResource();

    SINK_REFERENCE_PROPERTY(SinkAccount, Account, account);
    SINK_PROPERTY(QByteArray, ResourceType, type);
    SINK_PROPERTY(QByteArrayList, Capabilities, capabilities);
    SINK_STATUS_PROPERTY(int, Status, status);
};

struct SINK_EXPORT Entity : public ApplicationDomainType {
    typedef QSharedPointer<Entity> Ptr;
    using ApplicationDomainType::ApplicationDomainType;
    Entity() = default;
    Entity(const ApplicationDomainType &other) : ApplicationDomainType(other) {}
    virtual ~Entity() = default;
};

struct SINK_EXPORT Addressbook : public Entity {
    SINK_ENTITY(Addressbook, addressbook);
    SINK_REFERENCE_PROPERTY(Addressbook, Parent, parent);
    SINK_PROPERTY(QString, Name, name);
    SINK_EXTRACTED_PROPERTY(QDateTime, LastUpdated, lastUpdated);
};

struct SINK_EXPORT Contact : public Entity {
    struct SINK_EXPORT Email {
        enum Type {
            Undefined,
            Work,
            Home
        };
        Type type;
        QString email;
    };
    SINK_ENTITY(Contact, contact);
    SINK_PROPERTY(QString, Uid, uid);
    SINK_PROPERTY(QString, Fn, fn);
    SINK_PROPERTY(QString, Firstname, firstname);
    SINK_PROPERTY(QString, Lastname, lastname);
    SINK_PROPERTY(QList<Email>, Emails, emails);
    SINK_PROPERTY(QByteArray, Vcard, vcard);
    SINK_PROPERTY(QByteArray, Photo, photo);
    SINK_REFERENCE_PROPERTY(Addressbook, Addressbook, addressbook);
};

struct SINK_EXPORT Event : public Entity {
    SINK_ENTITY(Event, event);
    SINK_PROPERTY(QString, Uid, uid);
    SINK_PROPERTY(QString, Summary, summary);
    SINK_PROPERTY(QString, Description, description);
    SINK_PROPERTY(QByteArray, Attachment, attachment);
};

struct SINK_EXPORT Todo : public Entity {
    SINK_ENTITY(Todo, todo);
};

struct SINK_EXPORT Calendar : public Entity {
    SINK_ENTITY(Calendar, calendar);
};

struct SINK_EXPORT Folder : public Entity {
    SINK_ENTITY(Folder, folder);
    SINK_REFERENCE_PROPERTY(Folder, Parent, parent);
    SINK_PROPERTY(QString, Name, name);
    SINK_PROPERTY(QByteArray, Icon, icon);
    SINK_PROPERTY(QByteArrayList, SpecialPurpose, specialpurpose);
    SINK_PROPERTY(bool, Enabled, enabled);
    SINK_EXTRACTED_PROPERTY(QDateTime, LastUpdated, lastUpdated);
    SINK_EXTRACTED_PROPERTY(int, Count, count);
    SINK_EXTRACTED_PROPERTY(bool, FullContentAvailable, fullContentAvailable);
};

struct SINK_EXPORT Mail : public Entity {
    struct SINK_EXPORT Contact {
        QString name;
        QString emailAddress;
    };

    SINK_ENTITY(Mail, mail);
    SINK_EXTRACTED_PROPERTY(Contact, Sender, sender);
    SINK_EXTRACTED_PROPERTY(QList<Contact>, To, to);
    SINK_EXTRACTED_PROPERTY(QList<Contact>, Cc, cc);
    SINK_EXTRACTED_PROPERTY(QList<Contact>, Bcc, bcc);
    SINK_EXTRACTED_PROPERTY(QString, Subject, subject);
    SINK_EXTRACTED_PROPERTY(QDateTime, Date, date);
    SINK_PROPERTY(bool, Unread, unread);
    SINK_PROPERTY(bool, Important, important);
    SINK_REFERENCE_PROPERTY(Folder, Folder, folder);
    SINK_PROPERTY(QByteArray, MimeMessage, mimeMessage);
    SINK_EXTRACTED_PROPERTY(bool, FullPayloadAvailable, fullPayloadAvailable);
    SINK_PROPERTY(bool, Draft, draft);
    SINK_PROPERTY(bool, Trash, trash);
    SINK_PROPERTY(bool, Sent, sent);
    SINK_EXTRACTED_PROPERTY(QByteArray, MessageId, messageId);
    SINK_EXTRACTED_PROPERTY(QByteArray, ParentMessageId, parentMessageId);
    SINK_INDEX_PROPERTY(QByteArray, ThreadId, threadId);
};

SINK_EXPORT QDebug operator<< (QDebug d, const Mail::Contact &c);

struct SINK_EXPORT Identity : public ApplicationDomainType {
    static constexpr const char *name = "identity";
    typedef QSharedPointer<Identity> Ptr;
    explicit Identity(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor);
    explicit Identity(const QByteArray &identifier);
    Identity();
    virtual ~Identity();
    SINK_REFERENCE_PROPERTY(SinkAccount, Account, account);
    SINK_PROPERTY(QString, Name, name);
    SINK_PROPERTY(QString, Address, address);
};

struct SINK_EXPORT DummyResource {
    static SinkResource create(const QByteArray &account);
};

struct SINK_EXPORT MaildirResource {
    static SinkResource create(const QByteArray &account);
};

struct SINK_EXPORT MailtransportResource {
    static SinkResource create(const QByteArray &account);
};

struct SINK_EXPORT ImapResource {
    static SinkResource create(const QByteArray &account);
};

struct SINK_EXPORT CardDavResource {
    static SinkResource create(const QByteArray &account);
};

namespace ResourceCapabilities {
namespace Mail {
    static constexpr const char *mail = "mail";
    static constexpr const char *folder = "folder";
    static constexpr const char *storage = "mail.storage";
    static constexpr const char *drafts = "mail.drafts";
    static constexpr const char *sent = "mail.sent";
    static constexpr const char *trash = "mail.trash";
    static constexpr const char *transport = "mail.transport";
    static constexpr const char *folderhierarchy = "mail.folderhierarchy";
};
namespace Contact {
    static constexpr const char *contact = "contact";
    static constexpr const char *addressbook = "addressbook";
    static constexpr const char *storage = "contact.storage";
};
};

namespace SpecialPurpose {
namespace Mail {
    static constexpr const char *inbox = "inbox";
    static constexpr const char *drafts = "drafts";
    static constexpr const char *trash = "trash";
    static constexpr const char *sent = "sent";
};
};

/**
 * All types need to be registered here an MUST return a different name.
 *
 * Do not store these types to disk, they may change over time.
 */
template<class DomainType>
QByteArray SINK_EXPORT getTypeName()
{
    return DomainType::name;
}

QByteArrayList SINK_EXPORT getTypeNames();

bool SINK_EXPORT isGlobalType(const QByteArray &type);


/**
 * Type implementation.
 * 
 * Needs to be implemented for every application domain type.
 * Contains all non-resource specific, but type-specific code.
 */
template<typename DomainType>
class SINK_EXPORT TypeImplementation;

}
}

#undef SINK_ENTITY
#undef SINK_PROPERTY
#undef SINK_EXTRACTED_PROPERTY
#undef SINK_REFERENCE_PROPERTY
#undef SINK_INDEX_PROPERTY

/**
 * This macro can be used to instantiate templates for all domain types.
 */
#define SINK_REGISTER_TYPES() \
    REGISTER_TYPE(Sink::ApplicationDomain::Contact) \
    REGISTER_TYPE(Sink::ApplicationDomain::Addressbook) \
    REGISTER_TYPE(Sink::ApplicationDomain::Event) \
    REGISTER_TYPE(Sink::ApplicationDomain::Mail) \
    REGISTER_TYPE(Sink::ApplicationDomain::Folder) \
    REGISTER_TYPE(Sink::ApplicationDomain::SinkResource) \
    REGISTER_TYPE(Sink::ApplicationDomain::SinkAccount) \
    REGISTER_TYPE(Sink::ApplicationDomain::Identity) \

SINK_EXPORT QDataStream &operator<<(QDataStream &out, const Sink::ApplicationDomain::Reference &reference);
SINK_EXPORT QDataStream &operator>>(QDataStream &in, Sink::ApplicationDomain::Reference &reference);

#define REGISTER_TYPE(TYPE) \
    Q_DECLARE_METATYPE(TYPE) \
    Q_DECLARE_METATYPE(TYPE::Ptr)
SINK_REGISTER_TYPES()
#undef REGISTER_TYPE

Q_DECLARE_METATYPE(Sink::ApplicationDomain::ApplicationDomainType)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::ApplicationDomainType::Ptr)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Entity)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Entity::Ptr)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Mail::Contact)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Contact::Email)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Error)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Progress)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Reference)
