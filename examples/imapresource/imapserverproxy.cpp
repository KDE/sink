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
#include <KIMAP/KIMAP/SelectJob>
#include <KIMAP/KIMAP/AppendJob>

#include <KIMAP/KIMAP/SessionUiProxy>
#include <KCoreAddons/KJob>

#include "log.h"

static KAsync::Job<void> runJob(KJob *job)
{
    return KAsync::start<void>([job](KAsync::Future<void> &future) {
        QObject::connect(job, &KJob::result, job, [&future](KJob *job) {
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

ImapServerProxy::ImapServerProxy(const QString &serverUrl, int port) : mSession(new KIMAP::Session(serverUrl, port))
{
    mSession->setUiProxy(SessionUiProxy::Ptr(new SessionUiProxy));
    mSession->setTimeout(10);
}

KAsync::Job<void> ImapServerProxy::login(const QString &username, const QString &password)
{
    if (mSession->state() == KIMAP::Session::State::Authenticated || mSession->state() == KIMAP::Session::State::Selected) {
        return KAsync::null<void>();
    }
    auto loginJob = new KIMAP::LoginJob(mSession);
    loginJob->setUserName(username);
    loginJob->setPassword(password);
    loginJob->setAuthenticationMode(KIMAP::LoginJob::Plain);
    loginJob->setEncryptionMode(KIMAP::LoginJob::EncryptionMode::AnySslVersion);
    return runJob(loginJob);
}

KAsync::Job<void> ImapServerProxy::select(const QString &mailbox)
{
    if (mSession->state() == KIMAP::Session::State::Disconnected) {
        return KAsync::error<void>(1, "Not connected");
    }
    auto select = new KIMAP::SelectJob(mSession);
    select->setMailBox(mailbox);
    // select->setCondstoreEnabled(serverSupportsCondstore());
    return runJob(select);
}

KAsync::Job<void> ImapServerProxy::append(const QString &mailbox, const QByteArray &content, const QList<QByteArray> &flags, const QDateTime &internalDate)
{
    if (mSession->state() == KIMAP::Session::State::Disconnected) {
        return KAsync::error<void>(1, "Not connected");
    }
    auto append = new KIMAP::AppendJob(mSession);
    append->setMailBox(mailbox);
    append->setContent(content);
    append->setFlags(flags);
    append->setInternalDate(internalDate);
    return runJob(append);
}

KAsync::Job<void> ImapServerProxy::fetch(const KIMAP::ImapSet &set, KIMAP::FetchJob::FetchScope scope, FetchCallback callback)
{
    if (mSession->state() == KIMAP::Session::State::Disconnected) {
        return KAsync::error<void>(1, "Not connected");
    }
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

KAsync::Future<void> ImapServerProxy::fetchFolders(std::function<void(const QStringList &)> callback)
{
    Trace() << "Fetching folders";
    auto job =  login("doe", "doe").then<void>(list(KIMAP::ListJob::IncludeUnsubscribed, [callback](const QList<KIMAP::MailBoxDescriptor> &mailboxes, const QList<QList<QByteArray> > &flags){
        QStringList list;
        for (const auto &mailbox : mailboxes) {
            Trace() << "Found mailbox: " << mailbox.name;
            list << mailbox.name;
        }
        callback(list);
    }),
    [](int errorCode, const QString &errorString) {
        Warning() << "Failed to list folders: " << errorCode << errorString;
    });
    return job.exec();
}

KAsync::Future<void> ImapServerProxy::fetchMessages(const QString &folder, std::function<void(const QVector<Message> &)> callback)
{
    auto job = login("doe", "doe").then<void>(select(folder)).then<void, KAsync::Job<void>>([this, callback, folder]() -> KAsync::Job<void> {
        return fetchHeaders(folder).then<void, KAsync::Job<void>, QList<qint64>>([this, callback](const QList<qint64> &uidsToFetch){
            Trace() << "Uids to fetch: " << uidsToFetch;
            if (uidsToFetch.isEmpty()) {
                Trace() << "Nothing to fetch";
                return KAsync::null<void>();
            }
            KIMAP::FetchJob::FetchScope scope;
            scope.parts.clear();
            scope.mode = KIMAP::FetchJob::FetchScope::Full;

            KIMAP::ImapSet set;
            set.add(uidsToFetch.toVector());
            return fetch(set, scope,
                    [callback](const QString &mailbox,
                            const QMap<qint64,qint64> &uids,
                            const QMap<qint64,qint64> &sizes,
                            const QMap<qint64,KIMAP::MessageAttribute> &attrs,
                            const QMap<qint64,KIMAP::MessageFlags> &flags,
                            const QMap<qint64,KIMAP::MessagePtr> &messages) {
                        Trace() << "Received " << uids.size() << " messages from " << mailbox;
                        Trace() << uids.size() << sizes.size() << attrs.size() << flags.size() << messages.size();

                        QVector<Message> list;
                        for (const auto &id : uids.keys()) {
                            list << Message{uids.value(id), sizes.value(id), attrs.value(id), flags.value(id), messages.value(id)};
                        }
                        callback(list);
                    });
        });

    });
    return job.exec();
}
