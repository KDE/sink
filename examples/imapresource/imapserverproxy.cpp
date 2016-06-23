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
#include "imapserverproxy.h"

#include <QDir>
#include <QFile>
#include <KIMAP/KIMAP/LoginJob>
#include <kimap/namespacejob.h>
#include <KIMAP/KIMAP/SelectJob>
#include <KIMAP/KIMAP/AppendJob>
#include <KIMAP/KIMAP/CreateJob>
#include <KIMAP/KIMAP/CopyJob>
#include <KIMAP/KIMAP/RenameJob>
#include <KIMAP/KIMAP/DeleteJob>
#include <KIMAP/KIMAP/StoreJob>
#include <KIMAP/KIMAP/ExpungeJob>
#include <KIMAP/KIMAP/CapabilitiesJob>

#include <KIMAP/KIMAP/SessionUiProxy>
#include <KCoreAddons/KJob>

#include "log.h"

using namespace Imap;

const char* Imap::Flags::Seen = "\\Seen";
const char* Imap::Flags::Deleted = "\\Deleted";
const char* Imap::Flags::Answered = "\\Answered";
const char* Imap::Flags::Flagged = "\\Flagged";

const char* Imap::FolderFlags::Noselect = "\\Noselect";
const char* Imap::FolderFlags::Noinferiors = "\\Noinferiors";
const char* Imap::FolderFlags::Marked = "\\Marked";
const char* Imap::FolderFlags::Unmarked = "\\Unmarked";

template <typename T>
static KAsync::Job<T> runJob(KJob *job, const std::function<T(KJob*)> &f)
{
    return KAsync::start<T>([job, f](KAsync::Future<T> &future) {
        QObject::connect(job, &KJob::result, [&future, f](KJob *job) {
            if (job->error()) {
                future.setError(job->error(), job->errorString());
            } else {
                future.setValue(f(job));
                future.setFinished();
            }
        });
        job->start();
    });
}

static KAsync::Job<void> runJob(KJob *job)
{
    return KAsync::start<void>([job](KAsync::Future<void> &future) {
        QObject::connect(job, &KJob::result, [&future](KJob *job) {
            if (job->error()) {
                Warning() << "Job failed: " << job->errorString();
                future.setError(job->error(), job->errorString());
            } else {
                future.setFinished();
            }
        });
        job->start();
    });
}

class SessionUiProxy : public KIMAP::SessionUiProxy {
  public:
    bool ignoreSslError( const KSslErrorUiData &errorData ) {
        return true;
    }
};

ImapServerProxy::ImapServerProxy(const QString &serverUrl, int port) : mSession(new KIMAP::Session(serverUrl, qint16(port)))
{
    mSession->setUiProxy(SessionUiProxy::Ptr(new SessionUiProxy));
    mSession->setTimeout(10);
}

KAsync::Job<void> ImapServerProxy::login(const QString &username, const QString &password)
{
    auto loginJob = new KIMAP::LoginJob(mSession);
    loginJob->setUserName(username);
    loginJob->setPassword(password);
    loginJob->setAuthenticationMode(KIMAP::LoginJob::Plain);
    if (mSession->port() == 143) {
        loginJob->setEncryptionMode(KIMAP::LoginJob::EncryptionMode::TlsV1);
    } else {
        loginJob->setEncryptionMode(KIMAP::LoginJob::EncryptionMode::AnySslVersion);
    }

    auto capabilitiesJob = new KIMAP::CapabilitiesJob(mSession);
    QObject::connect(capabilitiesJob, &KIMAP::CapabilitiesJob::capabilitiesReceived, [this](const QStringList &capabilities) {
        mCapabilities = capabilities;
    });
    auto namespaceJob = new KIMAP::NamespaceJob(mSession);

    return runJob(loginJob).then(runJob(capabilitiesJob)).then<void>([this](){
        Trace() << "Supported capabilities: " << mCapabilities;
        QStringList requiredExtensions = QStringList() << "UIDPLUS" << "NAMESPACE";
        for (const auto &requiredExtension : requiredExtensions) {
            if (!mCapabilities.contains(requiredExtension)) {
                Warning() << "Server doesn't support required capability: " << requiredExtension;
                //TODO fail the job
            }
        }
    }).then(runJob(namespaceJob)).then<void>([this, namespaceJob](){
        for (const auto &ns :namespaceJob->personalNamespaces()) {
            mPersonalNamespaces << ns.name;
            mPersonalNamespaceSeparator = ns.separator;
        }
        for (const auto &ns :namespaceJob->sharedNamespaces()) {
            mSharedNamespaces << ns.name;
            mSharedNamespaceSeparator = ns.separator;
        }
        for (const auto &ns :namespaceJob->userNamespaces()) {
            mUserNamespaces << ns.name;
            mUserNamespaceSeparator = ns.separator;
        }
        Trace() << "Found personal namespaces: " << mPersonalNamespaces << mPersonalNamespaceSeparator;
        Trace() << "Found shared namespaces: " << mSharedNamespaces << mSharedNamespaceSeparator;
        Trace() << "Found user namespaces: " << mUserNamespaces << mUserNamespaceSeparator;
    });
}

KAsync::Job<void> ImapServerProxy::select(const QString &mailbox)
{
    auto select = new KIMAP::SelectJob(mSession);
    select->setMailBox(mailbox);
    // select->setCondstoreEnabled(serverSupportsCondstore());
    return runJob(select);
}

KAsync::Job<qint64> ImapServerProxy::append(const QString &mailbox, const QByteArray &content, const QList<QByteArray> &flags, const QDateTime &internalDate)
{
    auto append = new KIMAP::AppendJob(mSession);
    append->setMailBox(mailbox);
    append->setContent(content);
    append->setFlags(flags);
    append->setInternalDate(internalDate);
    return runJob<qint64>(append, [](KJob *job) -> qint64{
        return static_cast<KIMAP::AppendJob*>(job)->uid();
    });
}

KAsync::Job<void> ImapServerProxy::store(const KIMAP::ImapSet &set, const QList<QByteArray> &flags)
{
    return storeFlags(set, flags);
}

KAsync::Job<void> ImapServerProxy::storeFlags(const KIMAP::ImapSet &set, const QList<QByteArray> &flags)
{
    auto store = new KIMAP::StoreJob(mSession);
    store->setUidBased(true);
    store->setMode(KIMAP::StoreJob::SetFlags);
    store->setSequenceSet(set);
    store->setFlags(flags);
    return runJob(store);
}

KAsync::Job<void> ImapServerProxy::addFlags(const KIMAP::ImapSet &set, const QList<QByteArray> &flags)
{
    auto store = new KIMAP::StoreJob(mSession);
    store->setUidBased(true);
    store->setMode(KIMAP::StoreJob::AppendFlags);
    store->setSequenceSet(set);
    store->setFlags(flags);
    return runJob(store);
}

KAsync::Job<void> ImapServerProxy::removeFlags(const KIMAP::ImapSet &set, const QList<QByteArray> &flags)
{
    auto store = new KIMAP::StoreJob(mSession);
    store->setUidBased(true);
    store->setMode(KIMAP::StoreJob::RemoveFlags);
    store->setSequenceSet(set);
    store->setFlags(flags);
    return runJob(store);
}

KAsync::Job<void> ImapServerProxy::create(const QString &mailbox)
{
    auto create = new KIMAP::CreateJob(mSession);
    create->setMailBox(mailbox);
    return runJob(create);
}

KAsync::Job<void> ImapServerProxy::rename(const QString &mailbox, const QString &newMailbox)
{
    auto rename = new KIMAP::RenameJob(mSession);
    rename->setSourceMailBox(mailbox);
    rename->setDestinationMailBox(newMailbox);
    return runJob(rename);
}

KAsync::Job<void> ImapServerProxy::remove(const QString &mailbox)
{
    auto job = new KIMAP::DeleteJob(mSession);
    job->setMailBox(mailbox);
    return runJob(job);
}

KAsync::Job<void> ImapServerProxy::expunge()
{
    auto job = new KIMAP::ExpungeJob(mSession);
    return runJob(job);
}

KAsync::Job<void> ImapServerProxy::expunge(const KIMAP::ImapSet &set)
{
    //FIXME implement UID EXPUNGE
    auto job = new KIMAP::ExpungeJob(mSession);
    return runJob(job);
}

KAsync::Job<void> ImapServerProxy::copy(const KIMAP::ImapSet &set, const QString &newMailbox)
{
    auto copy = new KIMAP::CopyJob(mSession);
    copy->setSequenceSet(set);
    copy->setUidBased(true);
    copy->setMailBox(newMailbox);
    return runJob(copy);
}

KAsync::Job<void> ImapServerProxy::fetch(const KIMAP::ImapSet &set, KIMAP::FetchJob::FetchScope scope, FetchCallback callback)
{
    auto fetch = new KIMAP::FetchJob(mSession);
    fetch->setSequenceSet(set);
    fetch->setUidBased(true);
    fetch->setScope(scope);
    QObject::connect(fetch, static_cast<void(KIMAP::FetchJob::*)(const QString &,
                    const QMap<qint64,qint64> &,
                    const QMap<qint64,qint64> &,
                    const QMap<qint64,KIMAP::MessageAttribute> &,
                    const QMap<qint64,KIMAP::MessageFlags> &,
                    const QMap<qint64,KIMAP::MessagePtr> &)>(&KIMAP::FetchJob::headersReceived),
            callback);
    return runJob(fetch);
}

KAsync::Job<void> ImapServerProxy::fetch(const KIMAP::ImapSet &set, KIMAP::FetchJob::FetchScope scope, const std::function<void(const QVector<Message> &)> &callback)
{
    return fetch(set, scope,
                    [callback](const QString &mailbox,
                            const QMap<qint64,qint64> &uids,
                            const QMap<qint64,qint64> &sizes,
                            const QMap<qint64,KIMAP::MessageAttribute> &attrs,
                            const QMap<qint64,KIMAP::MessageFlags> &flags,
                            const QMap<qint64,KIMAP::MessagePtr> &messages) {
                        QVector<Message> list;
                        for (const auto &id : uids.keys()) {
                            list << Message{uids.value(id), sizes.value(id), attrs.value(id), flags.value(id), messages.value(id)};
                        }
                        callback(list);
                    });
}

KAsync::Job<QList<qint64>> ImapServerProxy::fetchHeaders(const QString &mailbox)
{
    auto list = QSharedPointer<QList<qint64>>::create();
    KIMAP::FetchJob::FetchScope scope;
    scope.parts.clear();
    scope.mode = KIMAP::FetchJob::FetchScope::Headers;

    //Fetch headers of all messages
    return fetch(KIMAP::ImapSet(1, 0), scope,
            [list](const QString &mailbox,
                    const QMap<qint64,qint64> &uids,
                    const QMap<qint64,qint64> &sizes,
                    const QMap<qint64,KIMAP::MessageAttribute> &attrs,
                    const QMap<qint64,KIMAP::MessageFlags> &flags,
                    const QMap<qint64,KIMAP::MessagePtr> &messages) {
                Trace() << "Received " << uids.size() << " headers from " << mailbox;
                Trace() << uids.size() << sizes.size() << attrs.size() << flags.size() << messages.size();

                //TODO based on the data available here, figure out which messages to actually fetch
                //(we only fetched headers and structure so far)
                //We could i.e. build chunks to fetch based on the size

                for (const auto &id : uids.keys()) {
                    list->append(uids.value(id));
                }
            })
    .then<QList<qint64>>([list](){
        return *list;
    });
}

KAsync::Job<void> ImapServerProxy::list(KIMAP::ListJob::Option option, const std::function<void(const QList<KIMAP::MailBoxDescriptor> &mailboxes,const QList<QList<QByteArray> > &flags)> &callback)
{
    auto listJob = new KIMAP::ListJob(mSession);
    listJob->setOption(option);
    // listJob->setQueriedNamespaces(serverNamespaces());
    QObject::connect(listJob, &KIMAP::ListJob::mailBoxesReceived,
            listJob, callback);
    return runJob(listJob);
}

KAsync::Job<void> ImapServerProxy::remove(const QString &mailbox, const KIMAP::ImapSet &set)
{
    return select(mailbox).then<void>(store(set, QByteArrayList() << Flags::Deleted)).then<void>(expunge(set));
}

KAsync::Job<void> ImapServerProxy::remove(const QString &mailbox, const QByteArray &imapSet)
{
    const auto set = KIMAP::ImapSet::fromImapSequenceSet(imapSet);
    return remove(mailbox, set);
}


KAsync::Job<void> ImapServerProxy::move(const QString &mailbox, const KIMAP::ImapSet &set, const QString &newMailbox)
{
    return select(mailbox).then<void>(copy(set, newMailbox)).then<void>(store(set, QByteArrayList() << Flags::Deleted)).then<void>(expunge(set));
}

KAsync::Job<QString> ImapServerProxy::createSubfolder(const QString &parentMailbox, const QString &folderName)
{
    auto folder = QSharedPointer<QString>::create();
    return KAsync::start<void, KAsync::Job<void>>([this, parentMailbox, folderName, folder]() {
        Q_ASSERT(!mPersonalNamespaceSeparator.isNull());
        if (parentMailbox.isEmpty()) {
            *folder = mPersonalNamespaces.toList().first() + folderName;
        } else {
            *folder = parentMailbox + mPersonalNamespaceSeparator + folderName;
        }
        Trace() << "Creating subfolder: " << *folder;
        return create(*folder);
    })
    .then<QString>([=]() {
        return *folder;
    });
}

KAsync::Job<QString> ImapServerProxy::renameSubfolder(const QString &oldMailbox, const QString &newName)
{
    auto folder = QSharedPointer<QString>::create();
    return KAsync::start<void, KAsync::Job<void>>([this, oldMailbox, newName, folder]() {
        Q_ASSERT(!mPersonalNamespaceSeparator.isNull());
        auto parts = oldMailbox.split(mPersonalNamespaceSeparator);
        parts.removeLast();
        *folder = parts.join(mPersonalNamespaceSeparator) + mPersonalNamespaceSeparator + newName;
        Trace() << "Renaming subfolder: " << oldMailbox << *folder;
        return rename(oldMailbox, *folder);
    })
    .then<QString>([=]() {
        return *folder;
    });
}

KAsync::Job<void> ImapServerProxy::fetchFolders(std::function<void(const QVector<Folder> &)> callback)
{
    Trace() << "Fetching folders";
    return list(KIMAP::ListJob::IncludeUnsubscribed, [callback](const QList<KIMAP::MailBoxDescriptor> &mailboxes, const QList<QList<QByteArray> > &flags){
        QVector<Folder> list;
        for (int i = 0; i < mailboxes.size(); i++) {
            const auto mailbox = mailboxes[i];
            const auto mailboxFlags = flags[i];
            bool noselect = mailboxFlags.contains(QByteArray(FolderFlags::Noselect).toLower()) || mailboxFlags.contains(QByteArray(FolderFlags::Noselect));
            Log() << "Found mailbox: " << mailbox.name << mailboxFlags << FolderFlags::Noselect << noselect;
            list << Folder{mailbox.name.split(mailbox.separator), mailbox.name, mailbox.separator, noselect};
        }
        callback(list);
    });
}

QString ImapServerProxy::mailboxFromFolder(const Folder &folder) const
{
    Q_ASSERT(!mPersonalNamespaceSeparator.isNull());
    return folder.pathParts.join(mPersonalNamespaceSeparator);
}

KAsync::Job<void> ImapServerProxy::fetchMessages(const Folder &folder, std::function<void(const QVector<Message> &)> callback)
{
    Q_ASSERT(!mPersonalNamespaceSeparator.isNull());
    return select(mailboxFromFolder(folder)).then<void, KAsync::Job<void>>([this, callback, folder]() -> KAsync::Job<void> {
        return fetchHeaders(mailboxFromFolder(folder)).then<void, KAsync::Job<void>, QList<qint64>>([this, callback](const QList<qint64> &uidsToFetch){
            Trace() << "Uids to fetch: " << uidsToFetch;
            if (uidsToFetch.isEmpty()) {
                Trace() << "Nothing to fetch";
                callback(QVector<Message>());
                return KAsync::null<void>();
            }
            KIMAP::FetchJob::FetchScope scope;
            scope.parts.clear();
            scope.mode = KIMAP::FetchJob::FetchScope::Full;

            KIMAP::ImapSet set;
            set.add(uidsToFetch.toVector());
            return fetch(set, scope, callback);
        });

    });
}
