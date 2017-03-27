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

#pragma once

#include <KAsync/Async>

#include <KMime/KMime/KMimeMessage>
#include <KIMAP2/ListJob>
#include <KIMAP2/Session>
#include <KIMAP2/FetchJob>
#include <KIMAP2/SearchJob>

namespace Imap {

enum ErrorCode {
    NoError,
    CouldNotConnectError,
    SslHandshakeError
};

namespace Flags
{
    /// The flag for a message being seen (i.e. opened by user).
    extern const char* Seen;
    /// The flag for a message being deleted by the user.
    extern const char* Deleted;
    /// The flag for a message being replied to by the user.
    extern const char* Answered;
    /// The flag for a message being marked as flagged.
    extern const char* Flagged;
}

namespace FolderFlags
{
    extern const char* Noinferiors;
    extern const char* Noselect;
    extern const char* Marked;
    extern const char* Unmarked;
    extern const char* Subscribed;
    extern const char* Sent;
    extern const char* Trash;
    extern const char* Archive;
    extern const char* Junk;
    extern const char* Flagged;
    extern const char* All;
}

namespace Capabilities
{
    extern const char* Condstore;
    extern const char* Uidplus;
    extern const char* Namespace;
}

struct Message {
    qint64 uid;
    qint64 size;
    KIMAP2::MessageAttributes attributes;
    KIMAP2::MessageFlags flags;
    KMime::Message::Ptr msg;
    bool fullPayload;
};

struct Folder {
    Folder() = default;
    Folder(const QString &path, const QString &ns, const QChar &separator, bool noselect_, bool subscribed_, const QByteArrayList &flags_)
        : noselect(noselect_),
        subscribed(subscribed_),
        flags(flags_),
        mPath(path),
        mNamespace(ns),
        mSeparator(separator)
    {
    }

    Folder(const QString &path_)
        : mPath(path_)
    {
    }

    QString path() const
    {
        Q_ASSERT(!mPath.isEmpty());
        return mPath;
    }

    QString parentPath() const
    {
        Q_ASSERT(!mSeparator.isNull());
        auto parts = mPath.split(mSeparator);
        parts.removeLast();
        auto parentPath = parts.join(mSeparator);
        //Don't return the namespace for root folders as parent folder
        if (mNamespace.startsWith(parentPath)) {
            return QString{};
        }
        return parentPath;
    }

    QString name() const
    {
        auto pathParts = mPath.split(mSeparator);
        Q_ASSERT(!pathParts.isEmpty());
        return pathParts.last();
    }

    bool noselect = false;
    bool subscribed = false;
    QByteArrayList flags;

private:
    QString mPath;
    QString mNamespace;
    QChar mSeparator;
};

struct SelectResult {
    qint64 uidValidity;
    qint64 uidNext;
    quint64 highestModSequence;
};

class Namespaces {
public:
    QList<KIMAP2::MailBoxDescriptor> personal;
    QList<KIMAP2::MailBoxDescriptor> shared;
    QList<KIMAP2::MailBoxDescriptor> user;

    KIMAP2::MailBoxDescriptor getDefaultNamespace()
    {
        return personal.isEmpty() ? KIMAP2::MailBoxDescriptor{} : personal.first();
    }

    KIMAP2::MailBoxDescriptor getNamespace(const QString &mailbox)
    {
        for (const auto &ns : personal) {
            if (mailbox.startsWith(ns.name)) {
                return ns;
            }
        }
        for (const auto &ns : shared) {
            if (mailbox.startsWith(ns.name)) {
                return ns;
            }
        }
        for (const auto &ns : user) {
            if (mailbox.startsWith(ns.name)) {
                return ns;
            }
        }
        return KIMAP2::MailBoxDescriptor{};
    }
};

class CachedSession {
public:

    CachedSession() = default;
    CachedSession(KIMAP2::Session *session, const QStringList &cap, const Namespaces &ns) : mSession(session), mCapabilities(cap), mNamespaces(ns)
    {
    }

    bool operator==(const CachedSession &other) const
    {
        return mSession && (mSession == other.mSession);
    }

    bool isConnected()
    {
        return (mSession->state() == KIMAP2::Session::State::Authenticated || mSession->state() == KIMAP2::Session::State::Selected) ;
    }

    bool isValid()
    {
        return mSession;
    }

    KIMAP2::Session *mSession = nullptr;
    QStringList mCapabilities;
    Namespaces mNamespaces;
};

class SessionCache : public QObject {
    Q_OBJECT
public:
    void recycleSession(const CachedSession &session)
    {
        QObject::connect(session.mSession, &KIMAP2::Session::stateChanged, this, [this, session](KIMAP2::Session::State newState, KIMAP2::Session::State oldState) {
            if (newState == KIMAP2::Session::Disconnected) {
                mSessions.removeOne(session);
            }
        });
        mSessions << session;
    }

    CachedSession getSession()
    {
        while (!mSessions.isEmpty()) {
            auto session = mSessions.takeLast();
            if (session.isConnected()) {
                return session;
            }
        }
        return CachedSession{};
    }
private:
    QList<CachedSession> mSessions;
};

class ImapServerProxy {
    KIMAP2::Session *mSession;
    QStringList mCapabilities;
    Namespaces mNamespaces;


public:
    ImapServerProxy(const QString &serverUrl, int port, SessionCache *sessionCache = nullptr);

    //Standard IMAP calls
    KAsync::Job<void> login(const QString &username, const QString &password);
    KAsync::Job<void> logout();
    KAsync::Job<SelectResult> select(const QString &mailbox);
    KAsync::Job<qint64> append(const QString &mailbox, const QByteArray &content, const QList<QByteArray> &flags = QList<QByteArray>(), const QDateTime &internalDate = QDateTime());
    KAsync::Job<void> store(const KIMAP2::ImapSet &set, const QList<QByteArray> &flags);
    KAsync::Job<void> storeFlags(const KIMAP2::ImapSet &set, const QList<QByteArray> &flags);
    KAsync::Job<void> addFlags(const KIMAP2::ImapSet &set, const QList<QByteArray> &flags);
    KAsync::Job<void> removeFlags(const KIMAP2::ImapSet &set, const QList<QByteArray> &flags);
    KAsync::Job<void> create(const QString &mailbox);
    KAsync::Job<void> rename(const QString &mailbox, const QString &newMailbox);
    KAsync::Job<void> remove(const QString &mailbox);
    KAsync::Job<void> expunge();
    KAsync::Job<void> expunge(const KIMAP2::ImapSet &set);
    KAsync::Job<void> copy(const KIMAP2::ImapSet &set, const QString &newMailbox);
    KAsync::Job<QVector<qint64>> search(const KIMAP2::ImapSet &set);
    KAsync::Job<QVector<qint64>> search(const KIMAP2::Term &term);

    typedef std::function<void(const KIMAP2::FetchJob::Result &)> FetchCallback;

    KAsync::Job<void> fetch(const KIMAP2::ImapSet &set, KIMAP2::FetchJob::FetchScope scope, FetchCallback callback);
    KAsync::Job<void> fetch(const KIMAP2::ImapSet &set, KIMAP2::FetchJob::FetchScope scope, const std::function<void(const Message &)> &callback);
    KAsync::Job<void> list(KIMAP2::ListJob::Option option, const std::function<void(const KIMAP2::MailBoxDescriptor &mailboxes,const QList<QByteArray> &flags)> &callback);

    QStringList getCapabilities() const;

    //Composed calls that do login etc.
    KAsync::Job<QVector<qint64>> fetchHeaders(const QString &mailbox, qint64 minUid = 1);
    KAsync::Job<void> remove(const QString &mailbox, const KIMAP2::ImapSet &set);
    KAsync::Job<void> remove(const QString &mailbox, const QByteArray &imapSet);
    KAsync::Job<void> move(const QString &mailbox, const KIMAP2::ImapSet &set, const QString &newMailbox);
    KAsync::Job<QString> createSubfolder(const QString &parentMailbox, const QString &folderName);
    KAsync::Job<QString> renameSubfolder(const QString &mailbox, const QString &newName);
    KAsync::Job<QVector<qint64>> fetchUids(const QString &mailbox);
    KAsync::Job<QVector<qint64>> fetchUidsSince(const QString &mailbox, const QDate &since);

    QString mailboxFromFolder(const Folder &) const;

    KAsync::Job<void> fetchFolders(std::function<void(const Folder &)> callback);
    KAsync::Job<void> fetchMessages(const Folder &folder, std::function<void(const Message &)> callback, std::function<void(int, int)> progress = std::function<void(int, int)>());
    KAsync::Job<void> fetchMessages(const Folder &folder, qint64 uidNext, std::function<void(const Message &)> callback, std::function<void(int, int)> progress = std::function<void(int, int)>());
    KAsync::Job<void> fetchMessages(const Folder &folder, const QVector<qint64> &uidsToFetch, bool headersOnly, std::function<void(const Message &)> callback, std::function<void(int, int)> progress);
    KAsync::Job<SelectResult> fetchFlags(const Folder &folder, const KIMAP2::ImapSet &set, qint64 changedsince, std::function<void(const Message &)> callback);
    KAsync::Job<QVector<qint64>> fetchUids(const Folder &folder);

private:
    QString getNamespace(const QString &name);
    QObject mGuard;
    SessionCache *mSessionCache;
};

}
