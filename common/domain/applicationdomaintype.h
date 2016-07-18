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
#include <QUuid>
#include "bufferadaptor.h"

#define SINK_ENTITY(TYPE) \
    typedef QSharedPointer<TYPE> Ptr; \
    using Entity::Entity; \
    virtual ~TYPE(); \
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

#define SINK_BLOB_PROPERTY(NAME, LOWERCASENAME) \
    struct NAME { \
        static constexpr const char *name = #LOWERCASENAME; \
        typedef QString Type; \
    }; \
    void set##NAME(const QByteArray &value) { setBlobProperty(NAME::name, value); } \
    void set##NAME##Path(const QString &path) { setProperty(NAME::name, QVariant::fromValue(path)); } \
    QByteArray get##NAME() const { return getBlobProperty(NAME::name); } \
    QString get##NAME##Path() const { return getProperty(NAME::name).value<QString>(); } \

#define SINK_REFERENCE_PROPERTY(TYPE, NAME, LOWERCASENAME) \
    struct NAME { \
        static constexpr const char *name = #LOWERCASENAME; \
        typedef QByteArray Type; \
    }; \
    void set##NAME(const ApplicationDomain::TYPE &value) { setProperty(NAME::name, value); } \
    void set##NAME(const QByteArray &value) { setProperty(NAME::name, QVariant::fromValue(value)); } \
    QByteArray get##NAME() const { return getProperty(NAME::name).value<QByteArray>(); } \


namespace Sink {
namespace ApplicationDomain {

struct SINK_EXPORT Error {

};

struct SINK_EXPORT Progress {

};

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

    template <typename DomainType>
    static typename DomainType::Ptr getInMemoryRepresentation(const ApplicationDomainType &domainType, const QList<QByteArray> properties = QList<QByteArray>())
    {
        auto memoryAdaptor = QSharedPointer<Sink::ApplicationDomain::MemoryBufferAdaptor>::create(*(domainType.mAdaptor), properties);
        //The identifier still internal refers to the memory-mapped pointer, we need to copy the memory or it will become invalid
        return QSharedPointer<DomainType>::create(domainType.mResourceInstanceIdentifier, QByteArray(domainType.mIdentifier.constData(), domainType.mIdentifier.size()), domainType.mRevision, memoryAdaptor);
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

    virtual ~ApplicationDomainType();

    bool hasProperty(const QByteArray &key) const;

    QVariant getProperty(const QByteArray &key) const;
    void setProperty(const QByteArray &key, const QVariant &value);
    void setProperty(const QByteArray &key, const ApplicationDomainType &value);

    QByteArray getBlobProperty(const QByteArray &key) const;
    void setBlobProperty(const QByteArray &key, const QByteArray &value);

    void setChangedProperties(const QSet<QByteArray> &changeset);
    QByteArrayList changedProperties() const;
    QByteArrayList availableProperties() const;
    qint64 revision() const;
    QByteArray resourceInstanceIdentifier() const;
    QByteArray identifier() const;

private:
    friend QDebug operator<<(QDebug, const ApplicationDomainType &);
    QSharedPointer<BufferAdaptor> mAdaptor;
    QSet<QByteArray> mChangeSet;
    /*
     * Each domain object needs to store the resource, identifier, revision triple so we can link back to the storage location.
     */
    QByteArray mResourceInstanceIdentifier;
    QByteArray mIdentifier;
    qint64 mRevision;
};

/*
 * Should this be specific to the synclistresultset, in other cases we may want to take revision and resource into account.
 */
inline bool operator==(const ApplicationDomainType& lhs, const ApplicationDomainType& rhs)
{
    return lhs.identifier() == rhs.identifier()
            && lhs.resourceInstanceIdentifier() == rhs.resourceInstanceIdentifier();
}

inline QDebug operator<< (QDebug d, const ApplicationDomainType &type)
{
    d << "ApplicationDomainType(\n";
    for (const auto &property : type.mAdaptor->availableProperties()) {
        d << " " << property << "\t" << type.getProperty(property) << "\n";
    }
    d << ")";
    return d;
}

struct SINK_EXPORT Entity : public ApplicationDomainType {
    typedef QSharedPointer<Entity> Ptr;
    using ApplicationDomainType::ApplicationDomainType;
    virtual ~Entity();
};

struct SINK_EXPORT Event : public Entity {
    SINK_ENTITY(Event);
    SINK_PROPERTY(QString, Uid, uid);
    SINK_PROPERTY(QString, Summary, summary);
    SINK_PROPERTY(QString, Description, description);
    SINK_PROPERTY(QByteArray, Attachment, attachment);
};

struct SINK_EXPORT Todo : public Entity {
    SINK_ENTITY(Todo);
};

struct SINK_EXPORT Calendar : public Entity {
    SINK_ENTITY(Calendar);
};

struct SINK_EXPORT Folder : public Entity {
    SINK_ENTITY(Folder);
    SINK_REFERENCE_PROPERTY(Folder, Parent, parent);
    SINK_PROPERTY(QString, Name, name);
    SINK_PROPERTY(QByteArray, Icon, icon);
    SINK_PROPERTY(QByteArrayList, SpecialPurpose, specialpurpose);
};

struct SINK_EXPORT Mail : public Entity {
    SINK_ENTITY(Mail);
    SINK_PROPERTY(QString, Uid, uid);
    SINK_EXTRACTED_PROPERTY(QString, Sender, sender);
    SINK_EXTRACTED_PROPERTY(QString, SenderName, senderName);
    SINK_EXTRACTED_PROPERTY(QString, Subject, subject);
    SINK_EXTRACTED_PROPERTY(QDateTime, Date, date);
    SINK_PROPERTY(bool, Unread, unread);
    SINK_PROPERTY(bool, Important, important);
    SINK_REFERENCE_PROPERTY(Folder, Folder, folder);
    SINK_BLOB_PROPERTY(MimeMessage, mimeMessage);
    SINK_PROPERTY(bool, Draft, draft);
    SINK_PROPERTY(bool, Trash, trash);
    SINK_PROPERTY(bool, Sent, sent);
};

/**
 * The status of an account or resource.
 *
 * It is set as follows:
 * * By default the status is offline.
 * * If a connection to the server could be established the status is Connected.
 * * If an error occurred that keeps the resource from operating (so non transient), the resource enters the error state.
 * * If a long running operation is started the resource goes to the busy state (and return to the previous state after that).
 */
enum SINK_EXPORT Status {
    OfflineStatus,
    ConnectedStatus,
    BusyStatus,
    ErrorStatus
};

struct SINK_EXPORT SinkAccount : public ApplicationDomainType {
    typedef QSharedPointer<SinkAccount> Ptr;
    explicit SinkAccount(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor);
    explicit SinkAccount(const QByteArray &identifier);
    SinkAccount();
    virtual ~SinkAccount();

    SINK_PROPERTY(QString, Name, name);
    SINK_PROPERTY(QString, Icon, icon);
    SINK_PROPERTY(QString, AccountType, accountType);
    SINK_STATUS_PROPERTY(int, Status, status);
    SINK_STATUS_PROPERTY(ApplicationDomain::Error, Error, error);
    SINK_STATUS_PROPERTY(ApplicationDomain::Progress, Progress, progress);
};


/**
 * Represents an sink resource.
 *
 * This type is used for configuration of resources,
 * and for creating and removing resource instances.
 */
struct SINK_EXPORT SinkResource : public ApplicationDomainType {
    typedef QSharedPointer<SinkResource> Ptr;
    explicit SinkResource(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor);
    explicit SinkResource(const QByteArray &identifier);
    SinkResource();
    virtual ~SinkResource();

    SINK_REFERENCE_PROPERTY(SinkAccount, Account, account);
    SINK_PROPERTY(QString, ResourceType, resourceType);
    SINK_PROPERTY(QByteArrayList, Capabilities, capabilities);
    SINK_STATUS_PROPERTY(int, Status, status);
    SINK_STATUS_PROPERTY(ApplicationDomain::Error, Error, error);
    SINK_STATUS_PROPERTY(ApplicationDomain::Progress, Progress, progress);
};

struct SINK_EXPORT Identity : public ApplicationDomainType {
    typedef QSharedPointer<Identity> Ptr;
    explicit Identity(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor);
    explicit Identity(const QByteArray &identifier);
    Identity();
    virtual ~Identity();
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

namespace ResourceCapabilities {
namespace Mail {
    static constexpr const char *storage = "mail.storage";
    static constexpr const char *drafts = "mail.drafts";
    static constexpr const char *trash = "mail.trash";
    static constexpr const char *transport = "mail.transport";
    static constexpr const char *folderhierarchy = "mail.folderhierarchy";
};
};

/**
 * All types need to be registered here an MUST return a different name.
 *
 * Do not store these types to disk, they may change over time.
 */
template<class DomainType>
QByteArray SINK_EXPORT getTypeName();

template<>
QByteArray SINK_EXPORT getTypeName<Event>();

template<>
QByteArray SINK_EXPORT getTypeName<Todo>();

template<>
QByteArray SINK_EXPORT getTypeName<SinkResource>();

template<>
QByteArray SINK_EXPORT getTypeName<SinkAccount>();

template<>
QByteArray SINK_EXPORT getTypeName<Identity>();

template<>
QByteArray SINK_EXPORT getTypeName<Mail>();

template<>
QByteArray SINK_EXPORT getTypeName<Folder>();

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
#undef SINK_BLOB_PROPERTY
#undef SINK_REFERENCE_PROPERTY

Q_DECLARE_METATYPE(Sink::ApplicationDomain::ApplicationDomainType)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::ApplicationDomainType::Ptr)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Entity)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Entity::Ptr)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Event)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Event::Ptr)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Mail)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Mail::Ptr)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Folder)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Folder::Ptr)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::SinkResource)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::SinkResource::Ptr)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::SinkAccount)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::SinkAccount::Ptr)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Identity)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Identity::Ptr)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Error)
Q_DECLARE_METATYPE(Sink::ApplicationDomain::Progress)
