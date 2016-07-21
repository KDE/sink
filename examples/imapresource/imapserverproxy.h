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

#include <Async/Async>

#include <KMime/KMime/KMimeMessage>
#include <KIMAP/KIMAP/ListJob>
#include <KIMAP/KIMAP/Session>
#include <KIMAP/KIMAP/FetchJob>

namespace Imap {

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
}

struct Message {
    qint64 uid;
    qint64 size;
    QPair<QByteArray, QVariant> attributes;
    QList<QByteArray> flags;
    KMime::Message::Ptr msg;
};

struct Folder {
    QString normalizedPath() const
    {
        return pathParts.join('/');
    }

    QString parentPath() const
    {
        auto parts = pathParts;
        parts.removeLast();
        return parts.join(separator);
    }

    QList<QString> pathParts;
    QString path;
    QChar separator;
    bool noselect;
};

struct SelectResult {
    qint64 uidValidity;
    qint64 uidNext;
    quint64 highestModSequence;
};

class ImapServerProxy {
    KIMAP::Session *mSession;
    QStringList mCapabilities;

    QSet<QString> mPersonalNamespaces;
    QChar mPersonalNamespaceSeparator;
    QSet<QString> mSharedNamespaces;
    QChar mSharedNamespaceSeparator;
    QSet<QString> mUserNamespaces;
    QChar mUserNamespaceSeparator;

public:
    ImapServerProxy(const QString &serverUrl, int port);

    //Standard IMAP calls
    KAsync::Job<void> login(const QString &username, const QString &password);
    KAsync::Job<SelectResult> select(const QString &mailbox);
    KAsync::Job<qint64> append(const QString &mailbox, const QByteArray &content, const QList<QByteArray> &flags = QList<QByteArray>(), const QDateTime &internalDate = QDateTime());
    KAsync::Job<void> store(const KIMAP::ImapSet &set, const QList<QByteArray> &flags);
    KAsync::Job<void> storeFlags(const KIMAP::ImapSet &set, const QList<QByteArray> &flags);
    KAsync::Job<void> addFlags(const KIMAP::ImapSet &set, const QList<QByteArray> &flags);
    KAsync::Job<void> removeFlags(const KIMAP::ImapSet &set, const QList<QByteArray> &flags);
    KAsync::Job<void> create(const QString &mailbox);
    KAsync::Job<void> rename(const QString &mailbox, const QString &newMailbox);
    KAsync::Job<void> remove(const QString &mailbox);
    KAsync::Job<void> expunge();
    KAsync::Job<void> expunge(const KIMAP::ImapSet &set);
    KAsync::Job<void> copy(const KIMAP::ImapSet &set, const QString &newMailbox);
    KAsync::Job<QVector<qint64>> search(const KIMAP::ImapSet &set);

    typedef std::function<void(const QString &,
                        const QMap<qint64,qint64> &,
                        const QMap<qint64,qint64> &,
                        const QMap<qint64,KIMAP::MessageAttribute> &,
                        const QMap<qint64,KIMAP::MessageFlags> &,
                        const QMap<qint64,KIMAP::MessagePtr> &)> FetchCallback;

    KAsync::Job<void> fetch(const KIMAP::ImapSet &set, KIMAP::FetchJob::FetchScope scope, FetchCallback callback);
    KAsync::Job<void> fetch(const KIMAP::ImapSet &set, KIMAP::FetchJob::FetchScope scope, const std::function<void(const QVector<Message> &)> &callback);
    KAsync::Job<void> list(KIMAP::ListJob::Option option, const std::function<void(const QList<KIMAP::MailBoxDescriptor> &mailboxes,const QList<QList<QByteArray> > &flags)> &callback);

    QStringList getCapabilities() const;

    //Composed calls that do login etc.
    KAsync::Job<QList<qint64>> fetchHeaders(const QString &mailbox);
    KAsync::Job<void> remove(const QString &mailbox, const KIMAP::ImapSet &set);
    KAsync::Job<void> remove(const QString &mailbox, const QByteArray &imapSet);
    KAsync::Job<void> move(const QString &mailbox, const KIMAP::ImapSet &set, const QString &newMailbox);
    KAsync::Job<QString> createSubfolder(const QString &parentMailbox, const QString &folderName);
    KAsync::Job<QString> renameSubfolder(const QString &mailbox, const QString &newName);
    KAsync::Job<QVector<qint64>> fetchUids(const QString &mailbox);

    QString mailboxFromFolder(const Folder &) const;

    KAsync::Job<void> fetchFolders(std::function<void(const QVector<Folder> &)> callback);
    KAsync::Job<void> fetchMessages(const Folder &folder, std::function<void(const QVector<Message> &)> callback, std::function<void(int, int)> progress = std::function<void(int, int)>());
    KAsync::Job<void> fetchMessages(const Folder &folder, qint64 uidNext, std::function<void(const QVector<Message> &)> callback, std::function<void(int, int)> progress = std::function<void(int, int)>());
    KAsync::Job<QVector<qint64>> fetchUids(const Folder &folder);

private:
    KAsync::Job<void> ping();

    QObject mGuard;
};

}
