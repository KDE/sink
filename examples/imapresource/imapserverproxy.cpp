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

#include <KIMAP2/LoginJob>
#include <KIMAP2/LogoutJob>
#include <KIMAP2/NamespaceJob>
#include <KIMAP2/SelectJob>
#include <KIMAP2/AppendJob>
#include <KIMAP2/CreateJob>
#include <KIMAP2/CopyJob>
#include <KIMAP2/RenameJob>
#include <KIMAP2/DeleteJob>
#include <KIMAP2/StoreJob>
#include <KIMAP2/ExpungeJob>
#include <KIMAP2/CapabilitiesJob>
#include <KIMAP2/SearchJob>
#include <KIMAP2/GetMetaDataJob>

#include <KCoreAddons/KJob>

#include "log.h"
#include "test.h"

using namespace Imap;

const char* Imap::Flags::Seen = "\\Seen";
const char* Imap::Flags::Deleted = "\\Deleted";
const char* Imap::Flags::Answered = "\\Answered";
const char* Imap::Flags::Flagged = "\\Flagged";

const char* Imap::FolderFlags::Noselect = "\\Noselect";
const char* Imap::FolderFlags::Noinferiors = "\\Noinferiors";
const char* Imap::FolderFlags::Marked = "\\Marked";
const char* Imap::FolderFlags::Unmarked = "\\Unmarked";
const char* Imap::FolderFlags::Subscribed = "\\Subscribed";
//Special use
const char* Imap::FolderFlags::Sent = "\\Sent";
const char* Imap::FolderFlags::Trash = "\\Trash";
const char* Imap::FolderFlags::Archive = "\\Archive";
const char* Imap::FolderFlags::Junk = "\\Junk";
const char* Imap::FolderFlags::Flagged = "\\Flagged";
const char* Imap::FolderFlags::Drafts = "\\Drafts";

const char* Imap::Capabilities::Namespace = "NAMESPACE";
const char* Imap::Capabilities::Uidplus = "UIDPLUS";
const char* Imap::Capabilities::Condstore = "CONDSTORE";

static int translateImapError(KJob *job)
{
    const int error = job->error();
    const bool isLoginJob = dynamic_cast<KIMAP2::LoginJob*>(job);
    const bool isSelectJob = dynamic_cast<KIMAP2::SelectJob*>(job);
    switch (error) {
        case KIMAP2::LoginJob::ErrorCode::ERR_HOST_NOT_FOUND:
            return Imap::HostNotFoundError;
        case KIMAP2::LoginJob::ErrorCode::ERR_COULD_NOT_CONNECT:
            return Imap::CouldNotConnectError;
        case KIMAP2::LoginJob::ErrorCode::ERR_SSL_HANDSHAKE_FAILED:
            return Imap::SslHandshakeError;
    }
    //Hack to detect login failures
    if (isLoginJob) {
        return Imap::LoginFailed;
    }
    //Hack to detect selection errors
    if (isSelectJob) {
        return Imap::SelectFailed;
    }
    //Hack to detect connection lost
    if (error == KJob::UserDefinedError) {
        return Imap::ConnectionLost;
    }
    return Imap::UnknownError;
}

template <typename T>
static KAsync::Job<T> runJob(KJob *job, const std::function<T(KJob*)> &f)
{
    return KAsync::start<T>([job, f](KAsync::Future<T> &future) {
        QObject::connect(job, &KJob::result, [&future, f](KJob *job) {
            SinkTrace() << "Job done: " << job->metaObject()->className();
            if (job->error()) {
                SinkWarning() << "Job failed: " << job->errorString() << job->metaObject()->className() << job->error();
                auto proxyError = translateImapError(job);
                future.setError(proxyError, job->errorString());
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
                SinkWarning() << "Job failed: " << job->errorString() << job->metaObject()->className() << job->error();
                auto proxyError = translateImapError(job);
                future.setError(proxyError, job->errorString());
            } else {
                future.setFinished();
            }
        });
        SinkTrace() << "Starting job: " << job->metaObject()->className();
        job->start();
    });
}

KIMAP2::Session *createNewSession(const QString &serverUrl, int port)
{
    auto newSession = new KIMAP2::Session(serverUrl, qint16(port));
    if (Sink::Test::testModeEnabled()) {
        newSession->setTimeout(1);
    } else {
        newSession->setTimeout(40);
    }
    QObject::connect(newSession, &KIMAP2::Session::sslErrors, [=](const QList<QSslError> &errors) {
        SinkLog() << "Received ssl error: " << errors;
        newSession->ignoreErrors(errors);
    });
    return newSession;
}

ImapServerProxy::ImapServerProxy(const QString &serverUrl, int port, EncryptionMode encryptionMode, SessionCache *sessionCache) : mSessionCache(sessionCache), mSession(nullptr), mEncryptionMode(encryptionMode)
{
    if (!mSessionCache || mSessionCache->isEmpty()) {
        mSession = createNewSession(serverUrl, port);
    }
}

QDebug operator<<(QDebug debug, const KIMAP2::MailBoxDescriptor &c)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << c.name;
    return debug;
}

KAsync::Job<void> ImapServerProxy::login(const QString &username, const QString &password)
{
    if (password.isEmpty()) {
        return KAsync::error(Imap::MissingCredentialsError);
    }
    if (mSessionCache) {
        auto session = mSessionCache->getSession();
        if (session.isValid()) {
            mSession = session.mSession;
            mCapabilities = session.mCapabilities;
            mNamespaces = session.mNamespaces;
        }
    }
    Q_ASSERT(mSession);
    if (mSession->state() == KIMAP2::Session::Authenticated || mSession->state() == KIMAP2::Session::Selected) {
        SinkLog() << "Reusing existing session.";
        return KAsync::null();
    }
    auto loginJob = new KIMAP2::LoginJob(mSession);
    loginJob->setUserName(username);
    loginJob->setPassword(password);
    if (mEncryptionMode == Starttls) {
        loginJob->setEncryptionMode(QSsl::TlsV1_0OrLater, true);
    } else if (mEncryptionMode == Tls) {
        loginJob->setEncryptionMode(QSsl::AnyProtocol, false);
    }
    loginJob->setAuthenticationMode(KIMAP2::LoginJob::Plain);

    auto capabilitiesJob = new KIMAP2::CapabilitiesJob(mSession);
    QObject::connect(capabilitiesJob, &KIMAP2::CapabilitiesJob::capabilitiesReceived, &mGuard, [this](const QStringList &capabilities) {
        mCapabilities = capabilities;
    });
    auto namespaceJob = new KIMAP2::NamespaceJob(mSession);

    return runJob(loginJob).then(runJob(capabilitiesJob)).then([this](){
        SinkTrace() << "Supported capabilities: " << mCapabilities;
        QStringList requiredExtensions = QStringList() << Capabilities::Uidplus << Capabilities::Namespace;
        for (const auto &requiredExtension : requiredExtensions) {
            if (!mCapabilities.contains(requiredExtension)) {
                SinkWarning() << "Server doesn't support required capability: " << requiredExtension;
                //TODO fail the job
            }
        }
    }).then(runJob(namespaceJob)).then([this, namespaceJob] {
        mNamespaces.personal = namespaceJob->personalNamespaces();
        mNamespaces.shared = namespaceJob->sharedNamespaces();
        mNamespaces.user = namespaceJob->userNamespaces();
        // SinkTrace() << "Found personal namespaces: " << mNamespaces.personal;
        // SinkTrace() << "Found shared namespaces: " << mNamespaces.shared;
        // SinkTrace() << "Found user namespaces: " << mNamespaces.user;
    });
}

KAsync::Job<void> ImapServerProxy::logout()
{
    if (mSessionCache) {
        auto session = CachedSession{mSession, mCapabilities, mNamespaces};
        if (session.isConnected()) {
            mSessionCache->recycleSession(session);
            return KAsync::null();
        }
    }
    if (mSession->state() == KIMAP2::Session::State::Authenticated || mSession->state() == KIMAP2::Session::State::Selected) {
        return runJob(new KIMAP2::LogoutJob(mSession));
    } else {
        return KAsync::null();
    }
}

bool ImapServerProxy::isGmail() const
{
    //Magic capability that only gmail has
    return mCapabilities.contains("X-GM-EXT-1");
}

KAsync::Job<SelectResult> ImapServerProxy::select(const QString &mailbox)
{
    auto select = new KIMAP2::SelectJob(mSession);
    select->setMailBox(mailbox);
    select->setCondstoreEnabled(mCapabilities.contains(Capabilities::Condstore));
    return runJob<SelectResult>(select, [select](KJob* job) -> SelectResult {
        return {select->uidValidity(), select->nextUid(), select->highestModSequence()};
    }).onError([=] (const KAsync::Error &error) {
        SinkWarning() << "Select failed: " << mailbox;
    });
}

KAsync::Job<SelectResult> ImapServerProxy::select(const Folder &folder)
{
    return select(mailboxFromFolder(folder));
}

KAsync::Job<qint64> ImapServerProxy::append(const QString &mailbox, const QByteArray &content, const QList<QByteArray> &flags, const QDateTime &internalDate)
{
    auto append = new KIMAP2::AppendJob(mSession);
    append->setMailBox(mailbox);
    append->setContent(content);
    append->setFlags(flags);
    append->setInternalDate(internalDate);
    return runJob<qint64>(append, [](KJob *job) -> qint64{
        return static_cast<KIMAP2::AppendJob*>(job)->uid();
    });
}

KAsync::Job<void> ImapServerProxy::store(const KIMAP2::ImapSet &set, const QList<QByteArray> &flags)
{
    return storeFlags(set, flags);
}

KAsync::Job<void> ImapServerProxy::storeFlags(const KIMAP2::ImapSet &set, const QList<QByteArray> &flags)
{
    auto store = new KIMAP2::StoreJob(mSession);
    store->setUidBased(true);
    store->setMode(KIMAP2::StoreJob::SetFlags);
    store->setSequenceSet(set);
    store->setFlags(flags);
    return runJob(store);
}

KAsync::Job<void> ImapServerProxy::addFlags(const KIMAP2::ImapSet &set, const QList<QByteArray> &flags)
{
    auto store = new KIMAP2::StoreJob(mSession);
    store->setUidBased(true);
    store->setMode(KIMAP2::StoreJob::AppendFlags);
    store->setSequenceSet(set);
    store->setFlags(flags);
    return runJob(store);
}

KAsync::Job<void> ImapServerProxy::removeFlags(const KIMAP2::ImapSet &set, const QList<QByteArray> &flags)
{
    auto store = new KIMAP2::StoreJob(mSession);
    store->setUidBased(true);
    store->setMode(KIMAP2::StoreJob::RemoveFlags);
    store->setSequenceSet(set);
    store->setFlags(flags);
    return runJob(store);
}

KAsync::Job<void> ImapServerProxy::create(const QString &mailbox)
{
    auto create = new KIMAP2::CreateJob(mSession);
    create->setMailBox(mailbox);
    return runJob(create);
}

KAsync::Job<void> ImapServerProxy::rename(const QString &mailbox, const QString &newMailbox)
{
    auto rename = new KIMAP2::RenameJob(mSession);
    rename->setSourceMailBox(mailbox);
    rename->setDestinationMailBox(newMailbox);
    return runJob(rename);
}

KAsync::Job<void> ImapServerProxy::remove(const QString &mailbox)
{
    auto job = new KIMAP2::DeleteJob(mSession);
    job->setMailBox(mailbox);
    return runJob(job);
}

KAsync::Job<void> ImapServerProxy::expunge()
{
    auto job = new KIMAP2::ExpungeJob(mSession);
    return runJob(job);
}

KAsync::Job<void> ImapServerProxy::expunge(const KIMAP2::ImapSet &set)
{
    //FIXME implement UID EXPUNGE
    auto job = new KIMAP2::ExpungeJob(mSession);
    return runJob(job);
}

KAsync::Job<void> ImapServerProxy::copy(const KIMAP2::ImapSet &set, const QString &newMailbox)
{
    auto copy = new KIMAP2::CopyJob(mSession);
    copy->setSequenceSet(set);
    copy->setUidBased(true);
    copy->setMailBox(newMailbox);
    return runJob(copy);
}

KAsync::Job<void> ImapServerProxy::fetch(const KIMAP2::ImapSet &set, KIMAP2::FetchJob::FetchScope scope, FetchCallback callback)
{
    auto fetch = new KIMAP2::FetchJob(mSession);
    fetch->setSequenceSet(set);
    fetch->setUidBased(true);
    fetch->setScope(scope);
    fetch->setAvoidParsing(true);
    QObject::connect(fetch, &KIMAP2::FetchJob::resultReceived, callback);
    return runJob(fetch);
}

KAsync::Job<QVector<qint64>> ImapServerProxy::search(const KIMAP2::ImapSet &set)
{
    return search(KIMAP2::Term(KIMAP2::Term::Uid, set));
}

KAsync::Job<QVector<qint64>> ImapServerProxy::search(const KIMAP2::Term &term)
{
    auto search = new KIMAP2::SearchJob(mSession);
    search->setTerm(term);
    search->setUidBased(true);
    return runJob<QVector<qint64>>(search, [](KJob *job) -> QVector<qint64> {
        return static_cast<KIMAP2::SearchJob*>(job)->results();
    });
}

KAsync::Job<void> ImapServerProxy::fetch(const KIMAP2::ImapSet &set, KIMAP2::FetchJob::FetchScope scope, const std::function<void(const Message &)> &callback)
{
    const bool fullPayload = (scope.mode == KIMAP2::FetchJob::FetchScope::Full);
    return fetch(set, scope,
                    [callback, fullPayload](const KIMAP2::FetchJob::Result &result) {
                        callback(Message{result.uid, result.size, result.attributes, result.flags, result.message, fullPayload});
                    });
}

QStringList ImapServerProxy::getCapabilities() const
{
    return mCapabilities;
}

KAsync::Job<QVector<qint64>> ImapServerProxy::fetchHeaders(const QString &mailbox, const qint64 minUid)
{
    auto list = QSharedPointer<QVector<qint64>>::create();
    KIMAP2::FetchJob::FetchScope scope;
    scope.mode = KIMAP2::FetchJob::FetchScope::Flags;

    //Fetch headers of all messages
    return fetch(KIMAP2::ImapSet(minUid, 0), scope,
            [list](const KIMAP2::FetchJob::Result &result) {
                // SinkTrace() << "Received " << uids.size() << " headers from " << mailbox;
                // SinkTrace() << uids.size() << sizes.size() << attrs.size() << flags.size() << messages.size();

                //TODO based on the data available here, figure out which messages to actually fetch
                //(we only fetched headers and structure so far)
                //We could i.e. build chunks to fetch based on the size

                list->append(result.uid);
            })
    .then([list](){
        return *list;
    });
}

KAsync::Job<QVector<qint64>> ImapServerProxy::fetchUids(const QString &mailbox)
{
    auto notDeleted = KIMAP2::Term(KIMAP2::Term::Deleted);
    notDeleted.setNegated(true);
    return select(mailbox).then<QVector<qint64>>(search(notDeleted));
}

KAsync::Job<QVector<qint64>> ImapServerProxy::fetchUidsSince(const QString &mailbox, const QDate &since)
{
    auto sinceTerm = KIMAP2::Term(KIMAP2::Term::Since, since);
    auto notDeleted = KIMAP2::Term(KIMAP2::Term::Deleted);
    notDeleted.setNegated(true);
    auto term = KIMAP2::Term(KIMAP2::Term::And, QVector<KIMAP2::Term>() << sinceTerm << notDeleted);
    return select(mailbox).then<QVector<qint64>>(search(term));
}

KAsync::Job<void> ImapServerProxy::list(KIMAP2::ListJob::Option option, const std::function<void(const KIMAP2::MailBoxDescriptor &mailboxes, const QList<QByteArray> &flags)> &callback)
{
    auto listJob = new KIMAP2::ListJob(mSession);
    listJob->setOption(option);
    // listJob->setQueriedNamespaces(serverNamespaces());
    QObject::connect(listJob, &KIMAP2::ListJob::resultReceived,
            listJob, callback);
    return runJob(listJob);
}

KAsync::Job<void> ImapServerProxy::remove(const QString &mailbox, const KIMAP2::ImapSet &set)
{
    return select(mailbox).then<void>(store(set, QByteArrayList() << Flags::Deleted)).then<void>(expunge(set));
}

KAsync::Job<void> ImapServerProxy::remove(const QString &mailbox, const QByteArray &imapSet)
{
    const auto set = KIMAP2::ImapSet::fromImapSequenceSet(imapSet);
    return remove(mailbox, set);
}


KAsync::Job<void> ImapServerProxy::move(const QString &mailbox, const KIMAP2::ImapSet &set, const QString &newMailbox)
{
    return select(mailbox).then<void>(copy(set, newMailbox)).then<void>(store(set, QByteArrayList() << Flags::Deleted)).then<void>(expunge(set));
}

KAsync::Job<QString> ImapServerProxy::createSubfolder(const QString &parentMailbox, const QString &folderName)
{
    return KAsync::start<QString>([this, parentMailbox, folderName]() {
        QString folder;
        if (parentMailbox.isEmpty()) {
            auto ns = mNamespaces.getDefaultNamespace();
            folder = ns.name + folderName;
        } else {
            auto ns = mNamespaces.getNamespace(parentMailbox);
            folder = parentMailbox + ns.separator + folderName;
        }
        SinkTrace() << "Creating subfolder: " << folder;
        return create(folder)
            .then([=]() {
                return folder;
            });
    });
}

KAsync::Job<QString> ImapServerProxy::renameSubfolder(const QString &oldMailbox, const QString &newName)
{
    return KAsync::start<QString>([this, oldMailbox, newName] {
        auto ns = mNamespaces.getNamespace(oldMailbox);
        auto parts = oldMailbox.split(ns.separator);
        parts.removeLast();
        QString folder = parts.join(ns.separator) + ns.separator + newName;
        SinkTrace() << "Renaming subfolder: " << oldMailbox << folder;
        return rename(oldMailbox, folder)
            .then([=]() {
                return folder;
            });
    });
}

QString ImapServerProxy::getNamespace(const QString &name)
{
    auto ns = mNamespaces.getNamespace(name);
    return ns.name;
}

static bool caseInsensitiveContains(const QByteArray &f, const QByteArrayList &list) {
    return list.contains(f) || list.contains(f.toLower());
}

bool Imap::flagsContain(const QByteArray &f, const QByteArrayList &flags)
{
    return caseInsensitiveContains(f, flags);
}

static void reportFolder(const Folder &f, QSharedPointer<QSet<QString>> reportedList, std::function<void(const Folder &)> callback) {
    if (!reportedList->contains(f.path())) {
        reportedList->insert(f.path());
        auto c = f;
        c.noselect = true;
        callback(c);
        if (!f.parentPath().isEmpty()){
            reportFolder(f.parentFolder(), reportedList, callback);
        }
    }
}

KAsync::Job<void> ImapServerProxy::getMetaData(std::function<void(const QHash<QString, QMap<QByteArray, QByteArray> > &metadata)> callback)
{
    if (!mCapabilities.contains("METADATA")) {
        return KAsync::null();
    }
    KIMAP2::GetMetaDataJob *meta = new KIMAP2::GetMetaDataJob(mSession);
    meta->setMailBox(QLatin1String("*"));
    meta->setServerCapability( KIMAP2::MetaDataJobBase::Metadata );
    meta->setDepth(KIMAP2::GetMetaDataJob::AllLevels);
    meta->addRequestedEntry("/shared/vendor/kolab/folder-type");
    meta->addRequestedEntry("/private/vendor/kolab/folder-type");
    return runJob(meta).then<void>([callback, meta] () {
        callback(meta->allMetaDataForMailboxes());
    });
}

KAsync::Job<void> ImapServerProxy::fetchFolders(std::function<void(const Folder &)> callback)
{
    SinkTrace() << "Fetching folders";
    auto subscribedList = QSharedPointer<QSet<QString>>::create() ;
    auto reportedList = QSharedPointer<QSet<QString>>::create() ;
    auto metaData = QSharedPointer<QHash<QString, QMap<QByteArray, QByteArray>>>::create() ;
    return getMetaData([=] (const QHash<QString, QMap<QByteArray, QByteArray>> &m) {
        *metaData = m;
    }).then(list(KIMAP2::ListJob::NoOption, [=](const KIMAP2::MailBoxDescriptor &mailbox, const QList<QByteArray> &){
        *subscribedList << mailbox.name;
    })).then(list(KIMAP2::ListJob::IncludeUnsubscribed, [=](const KIMAP2::MailBoxDescriptor &mailbox, const QList<QByteArray> &flags) {
        bool noselect = caseInsensitiveContains(FolderFlags::Noselect, flags);
        bool subscribed = subscribedList->contains(mailbox.name);
        if (isGmail()) {
            bool inbox = mailbox.name.toLower() == "inbox";
            bool sent = caseInsensitiveContains(FolderFlags::Sent, flags);
            bool drafts = caseInsensitiveContains(FolderFlags::Drafts, flags);
            bool trash = caseInsensitiveContains(FolderFlags::Trash, flags);
            /**
             * Because gmail duplicates messages all over the place we only support a few selected folders for now that should be mostly exclusive.
             */
            if (!(inbox || sent || drafts || trash)) {
                return;
            }
        }
        SinkLog() << "Found mailbox: " << mailbox.name << flags << FolderFlags::Noselect << noselect  << " sub: " << subscribed;
        //Ignore all non-mail folders
        if (metaData->contains(mailbox.name)) {
            auto m = metaData->value(mailbox.name);
            auto sharedType = m.value("/shared/vendor/kolab/folder-type");
            auto privateType = m.value("/private/vendor/kolab/folder-type");
            auto type = !privateType.isEmpty() ? privateType : sharedType;
            if (!type.isEmpty() && !type.contains("mail")) {
                SinkLog() << "Skipping due to folder type: " << type;
                return;
            }
        }
        auto ns = getNamespace(mailbox.name);
        auto folder = Folder{mailbox.name, ns, mailbox.separator, noselect, subscribed, flags};

        //call callback for parents if that didn't already happen.
        //This is necessary because we can have missing bits in the hierarchy in IMAP, but this will not work in sink because we'd end up with an incomplete tree.
        if (!folder.parentPath().isEmpty() && !reportedList->contains(folder.parentPath())) {
            reportFolder(folder.parentFolder(), reportedList, callback);
        }
        reportedList->insert(folder.path());
        callback(folder);
    }));
}

QString ImapServerProxy::mailboxFromFolder(const Folder &folder) const
{
    Q_ASSERT(!folder.path().isEmpty());
    return folder.path();
}

KAsync::Job<SelectResult> ImapServerProxy::fetchFlags(const Folder &folder, const KIMAP2::ImapSet &set, qint64 changedsince, std::function<void(const Message &)> callback)
{
    SinkTrace() << "Fetching flags " << folder.path();
    return select(folder).then<SelectResult, SelectResult>([=](const SelectResult &selectResult) -> KAsync::Job<SelectResult> {
        SinkTrace() << "Modeseq " << folder.path() << selectResult.highestModSequence << changedsince;

        if (selectResult.highestModSequence == static_cast<quint64>(changedsince)) {
            SinkTrace()<< folder.path() << "Changedsince didn't change, nothing to do.";
            return KAsync::value<SelectResult>(selectResult);
        }

        SinkTrace() << "Fetching flags  " << folder.path() << set << selectResult.highestModSequence << changedsince;

        KIMAP2::FetchJob::FetchScope scope;
        scope.mode = KIMAP2::FetchJob::FetchScope::Flags;
        scope.changedSince = changedsince;

        return fetch(set, scope, callback).then([selectResult] {
            return selectResult;
        });
    });
}

KAsync::Job<void> ImapServerProxy::fetchMessages(const Folder &folder, qint64 uidNext, std::function<void(const Message &)> callback, std::function<void(int, int)> progress)
{
    auto time = QSharedPointer<QTime>::create();
    time->start();
    return select(folder).then<void, SelectResult>([this, callback, folder, time, progress, uidNext](const SelectResult &selectResult) -> KAsync::Job<void> {
        SinkTrace() << "UIDNEXT " << folder.path() << selectResult.uidNext << uidNext;
        if (selectResult.uidNext == (uidNext + 1)) {
            SinkTrace()<< folder.path() << "Uidnext didn't change, nothing to do.";
            return KAsync::null<void>();
        }

        SinkTrace() << "Fetching messages from  " << folder.path() << selectResult.uidNext << uidNext;
        return fetchHeaders(mailboxFromFolder(folder), (uidNext + 1)).then<void, QVector<qint64>>([this, callback, time, progress, folder](const QVector<qint64> &uidsToFetch){
            SinkTrace() << "Fetched headers" << folder.path();
            SinkTrace() << "  Total: " << uidsToFetch.size();
            SinkTrace() << "  Uids to fetch: " << uidsToFetch;
            SinkTrace() << "  Took: " << Sink::Log::TraceTime(time->elapsed());
            return fetchMessages(folder, uidsToFetch, false, callback, progress);
        });

    });
}

KAsync::Job<void> ImapServerProxy::fetchMessages(const Folder &folder, const QVector<qint64> &uidsToFetch, bool headersOnly, std::function<void(const Message &)> callback, std::function<void(int, int)> progress)
{
    auto time = QSharedPointer<QTime>::create();
    time->start();
    return select(folder).then<void, SelectResult>([this, callback, folder, time, progress, uidsToFetch, headersOnly](const SelectResult &selectResult) -> KAsync::Job<void> {

        SinkTrace() << "Fetching messages" << folder.path();
        SinkTrace() << "  Total: " << uidsToFetch.size();
        SinkTrace() << "  Uids to fetch: " << uidsToFetch;
        auto totalCount = uidsToFetch.size();
        if (progress) {
            progress(0, totalCount);
        }
        if (uidsToFetch.isEmpty()) {
            SinkTrace() << "Nothing to fetch";
            return KAsync::null<void>();
        }
        KIMAP2::FetchJob::FetchScope scope;
        scope.parts.clear();
        if (headersOnly) {
            scope.mode = KIMAP2::FetchJob::FetchScope::Headers;
        } else {
            scope.mode = KIMAP2::FetchJob::FetchScope::Full;
        }

        KIMAP2::ImapSet set;
        set.add(uidsToFetch);
        auto count = QSharedPointer<int>::create();
        return fetch(set, scope, [=](const Message &message) {
            *count += 1;
            if (progress) {
                progress(*count, totalCount);
            }
            callback(message);
        });
    })
    .then([time]() {
        SinkTrace() << "The fetch took: " << Sink::Log::TraceTime(time->elapsed());
    });
}

KAsync::Job<void> ImapServerProxy::fetchMessages(const Folder &folder, std::function<void(const Message &)> callback, std::function<void(int, int)> progress)
{
    return fetchMessages(folder, 0, callback, progress);
}

KAsync::Job<QVector<qint64>> ImapServerProxy::fetchUids(const Folder &folder)
{
    return fetchUids(mailboxFromFolder(folder));
}
