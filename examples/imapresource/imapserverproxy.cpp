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
#include <QTcpSocket>
#include <QTimer>
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
#include <KIMAP/KIMAP/SearchJob>

#include <KIMAP/KIMAP/SessionUiProxy>
#include <KCoreAddons/KJob>

#include "log.h"
#include "test.h"

SINK_DEBUG_AREA("imapserverproxy")

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
            SinkTrace() << "Job done: " << job->metaObject()->className();
            if (job->error()) {
                SinkWarning() << "Job failed: " << job->errorString();
                future.setError(job->error(), job->errorString());
            } else {
                future.setValue(f(job));
                future.setFinished();
            }
        });
        SinkTrace() << "Starting job: " << job->metaObject()->className();
        job->start();
    });
}

static KAsync::Job<void> runJob(KJob *job)
{
    return KAsync::start<void>([job](KAsync::Future<void> &future) {
        QObject::connect(job, &KJob::result, [&future](KJob *job) {
            SinkTrace() << "Job done: " << job->metaObject()->className();
            if (job->error()) {
                SinkWarning() << "Job failed: " << job->errorString();
                future.setError(job->error(), job->errorString());
            } else {
                future.setFinished();
            }
        });
        SinkTrace() << "Starting job: " << job->metaObject()->className();
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
    if (Sink::Test::testModeEnabled()) {
        mSession->setTimeout(1);
    } else {
        mSession->setTimeout(10);
    }
}

KAsync::Job<void> ImapServerProxy::ping()
{
    auto hostname = mSession->hostName();
    auto port = mSession->port();
    auto timeout = mSession->timeout() * 1000;
    return KAsync::start<void>([=](KAsync::Future<void> &future) {
        SinkTrace() << "Starting ping" << timeout;
        auto guard = QPointer<QObject>(new QObject);
        auto socket = QSharedPointer<QTcpSocket>::create();
        QObject::connect(socket.data(), &QTcpSocket::hostFound, guard, [guard, &future, socket]() {
            SinkTrace() << "Ping succeeded";
            delete guard;
            future.setFinished();
        });
        QObject::connect(socket.data(), static_cast<void(QTcpSocket::*)(QTcpSocket::SocketError)>(&QTcpSocket::error), guard, [guard, &future, socket](QTcpSocket::SocketError) {
            SinkWarning() << "Ping failed: ";
            delete guard;
            future.setError(1, "Error during connect");
        });
        if (!guard) {
            return;
        }
        socket->connectToHost(hostname, port);
        QTimer::singleShot(timeout, guard, [guard, &future, hostname, port, socket]() {
            if (guard) {
                SinkWarning() << "Ping failed: " << hostname << port;
                delete guard;
                future.setError(1, "Couldn't lookup host");
            }
        });
    });

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
    QObject::connect(capabilitiesJob, &KIMAP::CapabilitiesJob::capabilitiesReceived, &mGuard, [this](const QStringList &capabilities) {
        mCapabilities = capabilities;
    });
    auto namespaceJob = new KIMAP::NamespaceJob(mSession);

    //FIXME The ping is only required because the login job doesn't fail after the configured timeout
    return ping().then(runJob(loginJob)).then(runJob(capabilitiesJob)).syncThen<void>([this](){
        SinkTrace() << "Supported capabilities: " << mCapabilities;
        QStringList requiredExtensions = QStringList() << "UIDPLUS" << "NAMESPACE";
        for (const auto &requiredExtension : requiredExtensions) {
            if (!mCapabilities.contains(requiredExtension)) {
                SinkWarning() << "Server doesn't support required capability: " << requiredExtension;
                //TODO fail the job
            }
        }
    }).then(runJob(namespaceJob)).syncThen<void>([this, namespaceJob] {
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
        SinkTrace() << "Found personal namespaces: " << mPersonalNamespaces << mPersonalNamespaceSeparator;
        SinkTrace() << "Found shared namespaces: " << mSharedNamespaces << mSharedNamespaceSeparator;
        SinkTrace() << "Found user namespaces: " << mUserNamespaces << mUserNamespaceSeparator;
    });
}

KAsync::Job<SelectResult> ImapServerProxy::select(const QString &mailbox)
{
    auto select = new KIMAP::SelectJob(mSession);
    select->setMailBox(mailbox);
    select->setCondstoreEnabled(mCapabilities.contains("CONDSTORE"));
    return runJob<SelectResult>(select, [select](KJob* job) -> SelectResult {
        return {select->uidValidity(), select->nextUid(), select->highestModSequence()};
    });
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

KAsync::Job<QVector<qint64>> ImapServerProxy::search(const KIMAP::ImapSet &set)
{
    auto search = new KIMAP::SearchJob(mSession);
    search->setTerm(KIMAP::Term(KIMAP::Term::Uid, set));
    search->setUidBased(true);
    return runJob<QVector<qint64>>(search, [](KJob *job) -> QVector<qint64> {
        return static_cast<KIMAP::SearchJob*>(job)->results();
    });
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

QStringList ImapServerProxy::getCapabilities() const
{
    return mCapabilities;
}

KAsync::Job<QList<qint64>> ImapServerProxy::fetchHeaders(const QString &mailbox, const qint64 minUid)
{
    auto list = QSharedPointer<QList<qint64>>::create();
    KIMAP::FetchJob::FetchScope scope;
    scope.mode = KIMAP::FetchJob::FetchScope::Flags;

    //Fetch headers of all messages
    return fetch(KIMAP::ImapSet(minUid, 0), scope,
            [list](const QString &mailbox,
                    const QMap<qint64,qint64> &uids,
                    const QMap<qint64,qint64> &sizes,
                    const QMap<qint64,KIMAP::MessageAttribute> &attrs,
                    const QMap<qint64,KIMAP::MessageFlags> &flags,
                    const QMap<qint64,KIMAP::MessagePtr> &messages) {
                SinkTrace() << "Received " << uids.size() << " headers from " << mailbox;
                SinkTrace() << uids.size() << sizes.size() << attrs.size() << flags.size() << messages.size();

                //TODO based on the data available here, figure out which messages to actually fetch
                //(we only fetched headers and structure so far)
                //We could i.e. build chunks to fetch based on the size

                for (const auto &id : uids.keys()) {
                    list->append(uids.value(id));
                }
            })
    .syncThen<QList<qint64>>([list](){
        return *list;
    });
}

KAsync::Job<QVector<qint64>> ImapServerProxy::fetchUids(const QString &mailbox)
{
    return select(mailbox).then<QVector<qint64>>(search(KIMAP::ImapSet(1, 0)));
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
    return KAsync::start<QString>([this, parentMailbox, folderName]() {
        Q_ASSERT(!mPersonalNamespaceSeparator.isNull());
        auto folder = QSharedPointer<QString>::create();
        if (parentMailbox.isEmpty()) {
            *folder = mPersonalNamespaces.toList().first() + folderName;
        } else {
            *folder = parentMailbox + mPersonalNamespaceSeparator + folderName;
        }
        SinkTrace() << "Creating subfolder: " << *folder;
        return create(*folder)
            .syncThen<QString>([=]() {
                return *folder;
            });
    });
}

KAsync::Job<QString> ImapServerProxy::renameSubfolder(const QString &oldMailbox, const QString &newName)
{
    return KAsync::start<QString>([this, oldMailbox, newName] {
        Q_ASSERT(!mPersonalNamespaceSeparator.isNull());
        auto parts = oldMailbox.split(mPersonalNamespaceSeparator);
        parts.removeLast();
        auto folder = QSharedPointer<QString>::create(parts.join(mPersonalNamespaceSeparator) + mPersonalNamespaceSeparator + newName);
        SinkTrace() << "Renaming subfolder: " << oldMailbox << *folder;
        return rename(oldMailbox, *folder)
            .syncThen<QString>([=]() {
                return *folder;
            });
    });
}

KAsync::Job<void> ImapServerProxy::fetchFolders(std::function<void(const QVector<Folder> &)> callback)
{
    SinkTrace() << "Fetching folders";
    return list(KIMAP::ListJob::IncludeUnsubscribed, [callback](const QList<KIMAP::MailBoxDescriptor> &mailboxes, const QList<QList<QByteArray> > &flags){
        QVector<Folder> list;
        for (int i = 0; i < mailboxes.size(); i++) {
            const auto mailbox = mailboxes[i];
            const auto mailboxFlags = flags[i];
            bool noselect = mailboxFlags.contains(QByteArray(FolderFlags::Noselect).toLower()) || mailboxFlags.contains(QByteArray(FolderFlags::Noselect));
            SinkLog() << "Found mailbox: " << mailbox.name << mailboxFlags << FolderFlags::Noselect << noselect;
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

KAsync::Job<void> ImapServerProxy::fetchMessages(const Folder &folder, qint64 uidNext, std::function<void(const QVector<Message> &)> callback, std::function<void(int, int)> progress)
{
    auto time = QSharedPointer<QTime>::create();
    time->start();
    Q_ASSERT(!mPersonalNamespaceSeparator.isNull());
    return select(mailboxFromFolder(folder)).then<void, SelectResult>([this, callback, folder, time, progress, uidNext](const SelectResult &selectResult) -> KAsync::Job<void> {

        SinkLog() << "UIDNEXT " << selectResult.uidNext << uidNext;
        if (selectResult.uidNext == (uidNext + 1)) {
            SinkTrace() << "Uidnext didn't change, nothing to do.";
            return KAsync::null<void>();
        }

        return fetchHeaders(mailboxFromFolder(folder), (uidNext + 1)).then<void, QList<qint64>>([this, callback, time, progress](const QList<qint64> &uidsToFetch){
            SinkTrace() << "Fetched headers";
            SinkTrace() << "  Total: " << uidsToFetch.size();
            SinkTrace() << "  Uids to fetch: " << uidsToFetch;
            SinkTrace() << "  Took: " << Sink::Log::TraceTime(time->elapsed());
            auto totalCount = uidsToFetch.size();
            if (progress) {
                progress(0, totalCount);
            }
            if (uidsToFetch.isEmpty()) {
                SinkTrace() << "Nothing to fetch";
                callback(QVector<Message>());
                return KAsync::null<void>();
            }
            KIMAP::FetchJob::FetchScope scope;
            scope.parts.clear();
            scope.mode = KIMAP::FetchJob::FetchScope::Full;

            KIMAP::ImapSet set;
            set.add(uidsToFetch.toVector());
            auto count = QSharedPointer<int>::create();
            return fetch(set, scope, [=](const QVector<Message> &messages) {
                *count += messages.size();
                if (progress) {
                    progress(*count, totalCount);
                }
                callback(messages);
            });
        });

    });
}

KAsync::Job<void> ImapServerProxy::fetchMessages(const Folder &folder, std::function<void(const QVector<Message> &)> callback, std::function<void(int, int)> progress)
{
    return fetchMessages(folder, 0, callback, progress);
}

KAsync::Job<QVector<qint64>> ImapServerProxy::fetchUids(const Folder &folder)
{
    return fetchUids(mailboxFromFolder(folder));
}
