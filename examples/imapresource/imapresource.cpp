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

#include "imapresource.h"

#include "facade.h"
#include "resourceconfig.h"
#include "commands.h"
#include "index.h"
#include "log.h"
#include "definitions.h"
#include "inspection.h"
#include "synchronizer.h"
#include "inspector.h"
#include "query.h"

#include <QDate>
#include <QDateTime>
#include <QUrl>

#include "facadefactory.h"
#include "adaptorfactoryregistry.h"

#include "imapserverproxy.h"
#include "mailpreprocessor.h"
#include "specialpurposepreprocessor.h"

//This is the resources entity type, and not the domain type
#define ENTITY_TYPE_MAIL "mail"
#define ENTITY_TYPE_FOLDER "folder"

Q_DECLARE_METATYPE(QSharedPointer<Imap::ImapServerProxy>)

using namespace Imap;
using namespace Sink;

static qint64 sCommitInterval = 100;

static qint64 uidFromMailRid(const QByteArray &remoteId)
{
    auto ridParts = remoteId.split(':');
    Q_ASSERT(ridParts.size() == 2);
    return ridParts.last().toLongLong();
}

static QByteArray folderIdFromMailRid(const QByteArray &remoteId)
{
    auto ridParts = remoteId.split(':');
    Q_ASSERT(ridParts.size() == 2);
    return ridParts.first();
}

static QByteArray assembleMailRid(const QByteArray &folderLocalId, qint64 imapUid)
{
    return folderLocalId + ':' + QByteArray::number(imapUid);
}

static QByteArray assembleMailRid(const ApplicationDomain::Mail &mail, qint64 imapUid)
{
    return assembleMailRid(mail.getFolder(), imapUid);
}

static QByteArray folderRid(const Imap::Folder &folder)
{
    return folder.path().toUtf8();
}

static QByteArray parentRid(const Imap::Folder &folder)
{
    return folder.parentPath().toUtf8();
}

static QByteArray getSpecialPurposeType(const QByteArrayList &flags)
{
    if (Imap::flagsContain(Imap::FolderFlags::Trash, flags)) {
        return ApplicationDomain::SpecialPurpose::Mail::trash;
    }
    if (Imap::flagsContain(Imap::FolderFlags::Drafts, flags)) {
        return ApplicationDomain::SpecialPurpose::Mail::drafts;
    }
    if (Imap::flagsContain(Imap::FolderFlags::Sent, flags)) {
        return ApplicationDomain::SpecialPurpose::Mail::sent;
    }
    return {};
}

static bool hasSpecialPurposeFlag(const QByteArrayList &flags)
{
    return !getSpecialPurposeType(flags).isEmpty();
}


class ImapSynchronizer : public Sink::Synchronizer {
    Q_OBJECT
public:
    ImapSynchronizer(const ResourceContext &resourceContext)
        : Sink::Synchronizer(resourceContext)
    {

    }

    QByteArray createFolder(const Imap::Folder &f)
    {
        const auto parentFolderRid = parentRid(f);
        bool isToplevel = parentFolderRid.isEmpty();

        SinkTraceCtx(mLogCtx) << "Creating folder: " << f.name() << parentFolderRid << f.flags;

        const auto remoteId = folderRid(f);
        Sink::ApplicationDomain::Folder folder;
        folder.setName(f.name());
        folder.setIcon("folder");
        folder.setEnabled(f.subscribed && !f.noselect);
        const auto specialPurpose = [&] {
            if (hasSpecialPurposeFlag(f.flags)) {
                return getSpecialPurposeType(f.flags);
            } else if (SpecialPurpose::isSpecialPurposeFolderName(f.name()) && isToplevel) {
                return SpecialPurpose::getSpecialPurposeType(f.name());
            }
            return QByteArray{};
        }();
        if (!specialPurpose.isEmpty()) {
            folder.setSpecialPurpose({specialPurpose});
        }
        //Always show the inbox
        if (specialPurpose == ApplicationDomain::SpecialPurpose::Mail::inbox) {
            folder.setEnabled(true);
        }

        if (!isToplevel) {
            folder.setParent(syncStore().resolveRemoteId(ApplicationDomain::Folder::name, parentFolderRid));
        }
        createOrModify(ApplicationDomain::getTypeName<ApplicationDomain::Folder>(), remoteId, folder);
        return remoteId;
    }

    static bool contains(const QVector<Folder> &folderList, const QByteArray &remoteId)
    {
        for (const auto &folder : folderList) {
            if (folderRid(folder) == remoteId) {
                return true;
            }
        }
        return false;
    }

    void synchronizeFolders(const QVector<Folder> &folderList)
    {
        SinkTraceCtx(mLogCtx) << "Found folders " << folderList.size();

        scanForRemovals(ENTITY_TYPE_FOLDER,
            [&folderList](const QByteArray &remoteId) -> bool {
                return contains(folderList, remoteId);
            }
        );

        for (const auto &f : folderList) {
            createFolder(f);
        }
    }

    static void setFlags(Sink::ApplicationDomain::Mail &mail, const KIMAP2::MessageFlags &flags)
    {
        mail.setUnread(!flags.contains(Imap::Flags::Seen));
        mail.setImportant(flags.contains(Imap::Flags::Flagged));
    }

    static KIMAP2::MessageFlags getFlags(const Sink::ApplicationDomain::Mail &mail)
    {
        KIMAP2::MessageFlags flags;
        if (!mail.getUnread()) {
            flags << Imap::Flags::Seen;
        }
        if (mail.getImportant()) {
            flags << Imap::Flags::Flagged;
        }
        return flags;
    }

    void createOrModifyMail(const QByteArray &folderRid, const QByteArray &folderLocalId, const Message &message)
    {
        auto time = QSharedPointer<QTime>::create();
        time->start();
        SinkTraceCtx(mLogCtx) << "Importing new mail." << folderRid;

        const auto remoteId = assembleMailRid(folderLocalId, message.uid);

        Q_ASSERT(message.msg);
        SinkTraceCtx(mLogCtx) << "Found a mail " << remoteId << message.flags;

        auto mail = Sink::ApplicationDomain::Mail::create(mResourceInstanceIdentifier);
        mail.setFolder(folderLocalId);
        mail.setMimeMessage(message.msg->encodedContent(true));
        mail.setExtractedFullPayloadAvailable(message.fullPayload);
        setFlags(mail, message.flags);

        createOrModify(ENTITY_TYPE_MAIL, remoteId, mail);
        // const auto elapsed = time->elapsed();
        // SinkTraceCtx(mLogCtx) << "Synchronized " << count << " mails in " << folderRid << Sink::Log::TraceTime(elapsed) << " " << elapsed/qMax(count, 1) << " [ms/mail]";
    }

    void synchronizeRemovals(const QByteArray &folderRid, const QSet<qint64> &messages)
    {
        auto time = QSharedPointer<QTime>::create();
        time->start();
        const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, folderRid);
        if (folderLocalId.isEmpty()) {
            SinkWarning() << "Failed to lookup local id of: " << folderRid;
            return;
        }

        SinkTraceCtx(mLogCtx) << "Finding removed mail: " << folderLocalId << " remoteId: " << folderRid;

        int count = scanForRemovals(ENTITY_TYPE_MAIL,
            [&](const std::function<void(const QByteArray &)> &callback) {
                store().indexLookup<ApplicationDomain::Mail, ApplicationDomain::Mail::Folder>(folderLocalId, callback);
            },
            [&](const QByteArray &remoteId) {
                return messages.contains(uidFromMailRid(remoteId));
            }
        );

        const auto elapsed = time->elapsed();
        SinkLog() << "Removed " << count << " mails in " << folderRid << Sink::Log::TraceTime(elapsed) << " " << elapsed/qMax(count, 1) << " [ms/mail]";
    }

    KAsync::Job<void> fetchFolderContents(QSharedPointer<ImapServerProxy> imap, const Imap::Folder &folder, const QDate &dateFilter, const SelectResult &selectResult)
    {
        const auto folderRemoteId = folderRid(folder);
        const auto logCtx = mLogCtx.subContext(folder.path().toUtf8());

        bool ok = false;
        const auto changedsince = syncStore().readValue(folderRemoteId, "changedsince").toLongLong(&ok);

        //If modseq should change on any change.
        if (ok && selectResult.highestModSequence == static_cast<quint64>(changedsince)) {
            SinkLogCtx(logCtx) << folder.path() << "highestModSequence didn't change, nothing to do.";
            return KAsync::null();
        }

        //First we fetch flag changes for all messages. Since we don't know which messages are locally available we just get everything and only apply to what we have.
        return KAsync::start<qint64>([=] {
            const auto lastSeenUid = qMax(qint64{0}, syncStore().readValue(folderRemoteId, "uidnext").toLongLong() - 1);
            SinkLogCtx(logCtx) << "About to update flags" << folder.path() << "changedsince: " << changedsince << "last seen uid: " << lastSeenUid;
            //If we have any mails so far we start off by updating any changed flags using changedsince, unless we don't have any mails at all.
            if (ok && lastSeenUid >= 1) {
                return imap->fetchFlags(KIMAP2::ImapSet(1, lastSeenUid), changedsince, [=](const Message &message) {
                    const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, folderRemoteId);
                    const auto remoteId = assembleMailRid(folderLocalId, message.uid);

                    SinkLogCtx(logCtx) << "Updating mail flags " << remoteId << message.flags;

                    auto mail = Sink::ApplicationDomain::Mail::create(mResourceInstanceIdentifier);
                    setFlags(mail, message.flags);

                    modify(ENTITY_TYPE_MAIL, remoteId, mail);
                })
                .then<qint64>([=] {
                    SinkLogCtx(logCtx) << "Flags updated. New changedsince value: " << selectResult.highestModSequence;
                    syncStore().writeValue(folderRemoteId, "changedsince", QByteArray::number(selectResult.highestModSequence));
                    return selectResult.uidNext;
                });
            } else {
                //We hit this path on initial sync and simply record the current changedsince value
                return KAsync::start<qint64>([=] {
                    SinkLogCtx(logCtx) << "No flags to update. New changedsince value: " << selectResult.highestModSequence;
                    syncStore().writeValue(folderRemoteId, "changedsince", QByteArray::number(selectResult.highestModSequence));
                    return selectResult.uidNext;
                });
            }
        })
        //Next we synchronize the full set that is given by the date limit.
        //We fetch all data for this set.
        //This will also pull in any new messages in subsequent runs.
        .then([=] (qint64 serverUidNext){
            const auto lastSeenUid = syncStore().contains(folderRemoteId, "uidnext") ? qMax(qint64{0}, syncStore().readValue(folderRemoteId, "uidnext").toLongLong() - 1) : -1;
            auto job = [=] {
                if (dateFilter.isValid()) {
                    SinkLogCtx(logCtx) << "Fetching messages since: " << dateFilter  << " or uid: " << lastSeenUid;
                    //Avoid creating a gap if we didn't fetch messages older than dateFilter, but aren't in the initial fetch either
                    if (syncStore().contains(folderRemoteId, "uidnext")) {
                        return imap->fetchUidsSince(dateFilter, lastSeenUid + 1);
                    } else {
                        return imap->fetchUidsSince(dateFilter);
                    }
                } else {
                    SinkLogCtx(logCtx) << "Fetching messages.";
                    return imap->fetchUids();
                }
            }();
            return job.then([=](const QVector<qint64> &uidsToFetch) {
                SinkTraceCtx(logCtx) << "Received result set " << uidsToFetch;
                SinkTraceCtx(logCtx) << "About to fetch mail" << folder.path();

                //Make sure the uids are sorted in reverse order and drop everything below lastSeenUid (so we don't refetch what we already have)
                QVector<qint64> filteredAndSorted = uidsToFetch;
                std::sort(filteredAndSorted.begin(), filteredAndSorted.end(), std::greater<qint64>());
                //Only filter the set if we have a valid lastSeenUid. Otherwise we would miss uid 1
                if (lastSeenUid > 0) {
                    const auto lowerBound = std::lower_bound(filteredAndSorted.begin(), filteredAndSorted.end(), lastSeenUid, std::greater<qint64>());
                    if (lowerBound != filteredAndSorted.end()) {
                        filteredAndSorted.erase(lowerBound, filteredAndSorted.end());
                    }
                }

                if (filteredAndSorted.isEmpty()) {
                    SinkTraceCtx(logCtx) << "Nothing new to fetch for full set.";
                    if (serverUidNext) {
                        SinkLogCtx(logCtx) << "Storing the server side uidnext: " << serverUidNext << folder.path();
                        //If we don't receive a mail we should still record the updated uidnext value.
                        syncStore().writeValue(folderRemoteId, "uidnext", QByteArray::number(serverUidNext));
                    }
                    if (!syncStore().contains(folderRemoteId, "fullsetLowerbound")) {
                        syncStore().writeValue(folderRemoteId, "fullsetLowerbound", QByteArray::number(serverUidNext));
                    }
                    return KAsync::null();
                }

                const qint64 lowerBoundUid = filteredAndSorted.last();

                auto maxUid = QSharedPointer<qint64>::create(filteredAndSorted.first());
                SinkTraceCtx(logCtx) << "Uids to fetch for full set: " << filteredAndSorted;

                bool headersOnly = false;
                const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, folderRemoteId);
                return imap->fetchMessages(folder, filteredAndSorted, headersOnly, [=](const Message &m) {
                    if (*maxUid < m.uid) {
                        *maxUid = m.uid;
                    }
                    createOrModifyMail(folderRemoteId, folderLocalId, m);
                },
                [=](int progress, int total) {
                    reportProgress(progress, total, {folderLocalId});
                    //commit every 100 messages
                    if ((progress % sCommitInterval) == 0) {
                        commit();
                    }
                })
                .then([=] {
                    SinkLogCtx(logCtx) << "Highest found uid: " << *maxUid << folder.path() << " Full set lower bound: " << lowerBoundUid;
                    syncStore().writeValue(folderRemoteId, "uidnext", QByteArray::number(*maxUid + 1));
                    //Remember the lowest full message we fetched.
                    //This is used below to fetch headers for the rest.
                    if (!syncStore().contains(folderRemoteId, "fullsetLowerbound")) {
                        syncStore().writeValue(folderRemoteId, "fullsetLowerbound", QByteArray::number(lowerBoundUid));
                    }
                    commit();
                });
            });
        })
        //For all remaining messages we fetch the headers only
        //This is supposed to make all existing messages avialable with at least the headers only.
        //If we succeed this only needs to happen once (everything new is fetched above as full message).
        .then<void>([=] {
            bool ok = false;
            const auto latestHeaderFetched = syncStore().readValue(folderRemoteId, "latestHeaderFetched").toLongLong();
            const auto fullsetLowerbound = syncStore().readValue(folderRemoteId, "fullsetLowerbound").toLongLong(&ok);

            if (ok && latestHeaderFetched < fullsetLowerbound) {
                SinkLogCtx(logCtx) << "Fetching headers for all messages until " << fullsetLowerbound << ". Already available until " <<  latestHeaderFetched;

                return imap->fetchUids()
                .then([=] (const QVector<qint64> &uids) {
                    //sort in reverse order and remove everything greater than fullsetLowerbound.
                    //This gives us all emails for which we haven't fetched the full content yet.
                    QVector<qint64> toFetch = uids;
                    std::sort(toFetch.begin(), toFetch.end(), std::greater<qint64>());
                    if (fullsetLowerbound) {
                        auto upperBound = std::upper_bound(toFetch.begin(), toFetch.end(), fullsetLowerbound, std::greater<qint64>());
                        if (upperBound != toFetch.begin()) {
                            toFetch.erase(toFetch.begin(), upperBound);
                        }
                    }
                    SinkTraceCtx(logCtx) << "Uids to fetch for headers only: " << toFetch;

                    bool headersOnly = true;
                    const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, folderRemoteId);
                    return imap->fetchMessages(folder, toFetch, headersOnly, [=](const Message &m) {
                        createOrModifyMail(folderRemoteId, folderLocalId, m);
                    },
                    [=](int progress, int total) {
                        reportProgress(progress, total, {folderLocalId});
                        //commit every 100 messages
                        if ((progress % sCommitInterval) == 0) {
                            commit();
                        }
                    });
                })
                .then([=] {
                    SinkLogCtx(logCtx) << "Headers fetched for folder: " << folder.path();
                    syncStore().writeValue(folderRemoteId, "latestHeaderFetched", QByteArray::number(fullsetLowerbound));
                    commit();
                });

            } else {
                SinkLogCtx(logCtx) << "No additional headers to fetch.";
            }
            return KAsync::null();
        })
        //Finally remove messages that are no longer existing on the server.
        .then([=] {
            //TODO do an examine with QRESYNC and remove VANISHED messages if supported instead
            return imap->fetchUids().then([=](const QVector<qint64> &uids) {
                SinkTraceCtx(logCtx) << "Syncing removals: " << folder.path();
                synchronizeRemovals(folderRemoteId, uids.toList().toSet());
                commit();
            });
        });
    }

    KAsync::Job<SelectResult> examine(QSharedPointer<ImapServerProxy> imap, const Imap::Folder &folder)
    {
        const auto logCtx = mLogCtx.subContext(folder.path().toUtf8());
        const auto folderRemoteId = folderRid(folder);
        Q_ASSERT(!folderRemoteId.isEmpty());
        return imap->examine(folder)
            .then([=](const SelectResult &selectResult) {
                bool ok = false;
                const auto uidvalidity = syncStore().readValue(folderRemoteId, "uidvalidity").toLongLong(&ok);
                SinkTraceCtx(logCtx) << "Checking UIDVALIDITY. Local" << uidvalidity << "remote " << selectResult.uidValidity;
                if (ok && selectResult.uidValidity != uidvalidity) {
                    SinkWarningCtx(logCtx) << "UIDVALIDITY changed " << selectResult.uidValidity << uidvalidity;
                    syncStore().removePrefix(folderRemoteId);
                }
                syncStore().writeValue(folderRemoteId, "uidvalidity", QByteArray::number(selectResult.uidValidity));
                return KAsync::value(selectResult);
            });
    }

    KAsync::Job<void> synchronizeFolder(QSharedPointer<ImapServerProxy> imap, const Imap::Folder &folder, const QDate &dateFilter, bool countOnly)
    {
        const auto logCtx = mLogCtx.subContext(folder.path().toUtf8());
        SinkLogCtx(logCtx) << "Synchronizing mails in folder: " << folderRid(folder);
        const auto folderRemoteId = folderRid(folder);
        if (folder.path().isEmpty() || folderRemoteId.isEmpty()) {
            SinkWarningCtx(logCtx) << "Invalid folder " << folderRemoteId << folder.path();
            return KAsync::error<void>("Invalid folder");
        }

        //Start by checking if UIDVALIDITY is still correct
        return KAsync::start([=] {
            return examine(imap, folder)
                .then([=](const SelectResult &selectResult) {
                    if (countOnly) {
                        const auto uidNext = syncStore().readValue(folderRemoteId, "uidnext").toLongLong();
                        SinkTraceCtx(mLogCtx) << "Checking for new messages." << folderRemoteId << " Local uidnext: " << uidNext << " Server uidnext: " << selectResult.uidNext;
                        if (selectResult.uidNext > uidNext) {
                            const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, folderRemoteId);
                            emitNotification(Notification::Info, ApplicationDomain::NewContentAvailable, {}, {}, ENTITY_TYPE_FOLDER, {folderLocalId});
                        }
                        return KAsync::null();
                    }
                    return fetchFolderContents(imap, folder, dateFilter, selectResult);
                });
        });
    }

    Sink::QueryBase applyMailDefaults(const Sink::QueryBase &query)
    {
        if (mDaysToSync > 0) {
            auto defaultDateFilter = QDate::currentDate().addDays(0 - mDaysToSync);
            auto queryWithDefaults = query;
            if (!queryWithDefaults.hasFilter<ApplicationDomain::Mail::Date>()) {
                queryWithDefaults.filter(ApplicationDomain::Mail::Date::name, QVariant::fromValue(defaultDateFilter));
            }
            return queryWithDefaults;
        }
        return query;
    }

    QList<Synchronizer::SyncRequest> getSyncRequests(const Sink::QueryBase &query) Q_DECL_OVERRIDE
    {
        QList<Synchronizer::SyncRequest> list;
        if (query.type() == ApplicationDomain::getTypeName<ApplicationDomain::Mail>()) {
            auto request = Synchronizer::SyncRequest{applyMailDefaults(query)};
            if (query.hasFilter(ApplicationDomain::Mail::Folder::name)) {
                request.applicableEntities << query.getFilter(ApplicationDomain::Mail::Folder::name).value.toByteArray();
            }
            list << request;
        } else if (query.type() == ApplicationDomain::getTypeName<ApplicationDomain::Folder>()) {
            list << Synchronizer::SyncRequest{query};
            auto mailQuery = Sink::QueryBase(ApplicationDomain::getTypeName<ApplicationDomain::Mail>());
            //A pseudo property filter to express that we only need to know if there are new messages at all
            mailQuery.filter("countOnly", {true});
            list << Synchronizer::SyncRequest{mailQuery, QByteArray{}, Synchronizer::SyncRequest::RequestFlush};
        } else {
            list << Synchronizer::SyncRequest{Sink::QueryBase(ApplicationDomain::getTypeName<ApplicationDomain::Folder>())};
            //This request depends on the previous one so we flush first.
            list << Synchronizer::SyncRequest{applyMailDefaults(Sink::QueryBase(ApplicationDomain::getTypeName<ApplicationDomain::Mail>())), QByteArray{}, Synchronizer::SyncRequest::RequestFlush};
        }
        return list;
    }

    QByteArray getFolderFromLocalId(const QByteArray &id)
    {
        auto mailRemoteId = syncStore().resolveLocalId(ApplicationDomain::getTypeName<ApplicationDomain::Mail>(), id);
        if (mailRemoteId.isEmpty()) {
            return {};
        }
        return folderIdFromMailRid(mailRemoteId);
    }

    void mergeIntoQueue(const Synchronizer::SyncRequest &request, QList<Synchronizer::SyncRequest> &queue)  Q_DECL_OVERRIDE
    {
        auto isIndividualMailSync = [](const Synchronizer::SyncRequest &request) {
            if (request.requestType == SyncRequest::Synchronization) {
                const auto query = request.query;
                if (query.type() == ApplicationDomain::getTypeName<ApplicationDomain::Mail>()) {
                    return !query.ids().isEmpty();
                }
            }
            return false;

        };

        if (isIndividualMailSync(request)) {
            auto newId = request.query.ids().first();
            auto requestFolder = getFolderFromLocalId(newId);
            if (requestFolder.isEmpty()) {
                SinkWarningCtx(mLogCtx) << "Failed to find folder for local id. Ignoring request: " << request.query;
                return;
            }
            for (auto &r : queue) {
                if (isIndividualMailSync(r)) {
                    auto queueFolder = getFolderFromLocalId(r.query.ids().first());
                    if (requestFolder == queueFolder) {
                        //Merge
                        r.query.filter(newId);
                        SinkTrace() << "Merging request " << request.query;
                        SinkTrace() << " to " << r.query;
                        return;
                    }
                }
            }
        }
        queue << request;
    }

    KAsync::Job<void> login(const QSharedPointer<ImapServerProxy> &imap)
    {
        SinkTrace() << "Connecting to:" << mServer << mPort;
        SinkTrace() << "as:" << mUser;
        return imap->login(mUser, secret())
        .addToContext(imap);
    }

    KAsync::Job<QVector<Folder>> getFolderList(const QSharedPointer<ImapServerProxy> &imap, const Sink::QueryBase &query)
    {
        auto localIds = [&] {
            if (query.hasFilter<ApplicationDomain::Mail::Folder>()) {
                //If we have a folder filter fetch full payload of date-range & all headers
                return resolveFilter(query.getFilter<ApplicationDomain::Mail::Folder>());
            }
            Sink::Query folderQuery;
            folderQuery.setType<ApplicationDomain::Folder>();
            folderQuery.filter<ApplicationDomain::Folder::Enabled>(true);
            return resolveQuery(folderQuery);
        }();

        QVector<Folder> folders;
        auto folderRemoteIds = syncStore().resolveLocalIds(ApplicationDomain::getTypeName<ApplicationDomain::Folder>(), localIds);
        for (const auto &r : folderRemoteIds) {
            Q_ASSERT(!r.isEmpty());
            folders << Folder{r};
        }
        return KAsync::value(folders);
    }

    KAsync::Error getError(const KAsync::Error &error)
    {
        if (error) {
            switch(error.errorCode) {
                case Imap::CouldNotConnectError:
                    return {ApplicationDomain::ConnectionError, error.errorMessage};
                case Imap::SslHandshakeError:
                case Imap::LoginFailed:
                    return {ApplicationDomain::LoginError, error.errorMessage};
                case Imap::HostNotFoundError:
                    return {ApplicationDomain::NoServerError, error.errorMessage};
                case Imap::ConnectionLost:
                    return {ApplicationDomain::ConnectionLostError, error.errorMessage};
                case Imap::MissingCredentialsError:
                    return {ApplicationDomain::MissingCredentialsError, error.errorMessage};
                default:
                    return {ApplicationDomain::UnknownError, error.errorMessage};
            }
        }
        return {};
    }

    KAsync::Job<void> synchronizeWithSource(const Sink::QueryBase &query) Q_DECL_OVERRIDE
    {
        if (!QUrl{mServer}.isValid()) {
            return KAsync::error(ApplicationDomain::ConfigurationError, "Invalid server url: " + mServer);
        }
        auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode, mAuthenticationMode, &mSessionCache);
        if (query.type() == ApplicationDomain::getTypeName<ApplicationDomain::Folder>()) {
            return login(imap)
            .then([=] {
                auto folderList = QSharedPointer<QVector<Folder>>::create();
                return imap->fetchFolders([folderList](const Folder &folder) {
                    *folderList << folder;
                })
                .then([=]() {
                    synchronizeFolders(*folderList);
                    return KAsync::null();
                });
            })
            .then([=] (const KAsync::Error &error) {
                return imap->logout()
                    .then(KAsync::error(getError(error)));
            });
        } else if (query.type() == ApplicationDomain::getTypeName<ApplicationDomain::Mail>()) {
            //TODO
            //if we have a folder filter:
            //* execute the folder query and resolve the results to the remote identifier
            //* query only those folders
            //if we have a date filter:
            //* apply the date filter to the fetch
            //if we have no folder filter:
            //* fetch list of folders from server directly and sync (because we have no guarantee that the folder sync was already processed by the pipeline).
            return login(imap)
            .then([=] {
                if (!query.ids().isEmpty()) {
                    //If we have mail id's simply fetch the full payload of those mails
                    QVector<qint64> toFetch;
                    auto mailRemoteIds = syncStore().resolveLocalIds(ApplicationDomain::getTypeName<ApplicationDomain::Mail>(), query.ids());
                    QByteArray folderRemoteId;
                    for (const auto &r : mailRemoteIds) {
                        const auto folderLocalId = folderIdFromMailRid(r);
                        auto f = syncStore().resolveLocalId(ApplicationDomain::getTypeName<ApplicationDomain::Folder>(), folderLocalId);
                        if (folderRemoteId.isEmpty()) {
                            folderRemoteId = f;
                        } else {
                            if (folderRemoteId != f) {
                                SinkWarningCtx(mLogCtx) << "Not all messages come from the same folder " << r << folderRemoteId << ". Skipping message.";
                                continue;
                            }
                        }
                        toFetch << uidFromMailRid(r);
                    }
                    SinkLog() << "Fetching messages: " << toFetch << folderRemoteId;
                    bool headersOnly = false;
                    const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, folderRemoteId);
                    return imap->fetchMessages(Folder{folderRemoteId}, toFetch, headersOnly, [=](const Message &m) {
                        createOrModifyMail(folderRemoteId, folderLocalId, m);
                    },
                    [=](int progress, int total) {
                        reportProgress(progress, total, {folderLocalId});
                        //commit every 100 messages
                        if ((progress % sCommitInterval) == 0) {
                            commit();
                        }
                    });
                } else {
                    const QDate dateFilter = [&] {
                        auto filter = query.getFilter<ApplicationDomain::Mail::Date>();
                        if (filter.value.canConvert<QDate>()) {
                            SinkLog() << " with date-range " << filter.value.value<QDate>();
                            return filter.value.value<QDate>();
                        }
                        return QDate{};
                    }();

                    return getFolderList(imap, query)
                        .then([=](const QVector<Folder> &folders) {
                            auto job = KAsync::null<void>();
                            for (const auto &folder : folders) {
                                job = job.then([=] {
                                    if (aborting()) {
                                        return KAsync::null();
                                    }
                                    return synchronizeFolder(imap, folder, dateFilter, query.hasFilter("countOnly"))
                                        .then([=](const KAsync::Error &error) {
                                            if (error) {
                                                if (error.errorCode == Imap::CommandFailed) {
                                                    SinkWarning() << "Continuing after protocol error: " << folder.path() << "Error: " << error;
                                                    //Ignore protocol-level errors and continue
                                                    return KAsync::null();
                                                }
                                                SinkWarning() << "Aborting on error: " << folder.path() << "Error: " << error;
                                                //Abort otherwise, e.g. if we disconnected
                                                return KAsync::error(error);
                                            }
                                            return KAsync::null();
                                        });
                                });

                            }
                            return job;
                        });
                }
            })
            .then([=] (const KAsync::Error &error) {
                return imap->logout()
                    .then(KAsync::error(getError(error)));
            });
        }
        return KAsync::error<void>("Nothing to do");
    }
    static QByteArray ensureCRLF(const QByteArray &data) {
        auto index = data.indexOf('\n');
        if (index > 0 && data.at(index - 1) == '\r') { //First line is LF-only terminated
            //Convert back and forth in case there's a mix. We don't want to expand CRLF into CRCRLF.
            return KMime::LFtoCRLF(KMime::CRLFtoLF(data));
        } else {
            return data;
        }
    }

    static bool validateContent(const QByteArray &data) {
        if (data.isEmpty()) {
            SinkError() << "No data available.";
            return false;
        }
        if (data.contains('\0')) {
            SinkError() << "Data contains NUL, this will fail with IMAP.";
            return false;
        }
        return true;
    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Mail &mail, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        if (operation != Sink::Operation_Creation) {
            if(oldRemoteId.isEmpty()) {
                SinkWarning() << "Tried to replay modification without old remoteId.";
                // Since we can't recover from the situation we just skip over the revision.
                // This can for instance happen if creation failed, and we then process a removal or modification.
                return KAsync::null<QByteArray>();
            }
        }
        auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode, mAuthenticationMode, &mSessionCache);
        auto login = imap->login(mUser, secret());
        KAsync::Job<QByteArray> job = KAsync::null<QByteArray>();
        if (operation == Sink::Operation_Creation) {
            const QString mailbox = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, mail.getFolder());
            const auto content = ensureCRLF(mail.getMimeMessage());
            if (!validateContent(content)) {
                SinkError() << "Validation failed during creation replay " << mail.identifier() << "\n  Content:" << content;
                //We can't recover from this other than deleting the mail, so we skip it.
                return KAsync::null<QByteArray>();
            }
            const auto flags = getFlags(mail);
            const QDateTime internalDate = mail.getDate();
            job = login.then(imap->append(mailbox, content, flags, internalDate))
                .addToContext(imap)
                .then([mail](qint64 uid) {
                    const auto remoteId = assembleMailRid(mail, uid);
                    SinkTrace() << "Finished creating a new mail: " << remoteId;
                    return remoteId;
                });
        } else if (operation == Sink::Operation_Removal) {
            const auto folderId = folderIdFromMailRid(oldRemoteId);
            const QString mailbox = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, folderId);
            const auto uid = uidFromMailRid(oldRemoteId);
            SinkTrace() << "Removing a mail: " << oldRemoteId << "in the mailbox: " << mailbox;
            KIMAP2::ImapSet set;
            set.add(uid);
            job = login.then(imap->remove(mailbox, set))
                .then([imap, oldRemoteId] {
                    SinkTrace() << "Finished removing a mail: " << oldRemoteId;
                    return QByteArray();
                });
        } else if (operation == Sink::Operation_Modification) {
            const QString mailbox = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, mail.getFolder());
            const auto uid = uidFromMailRid(oldRemoteId);

            SinkTrace() << "Modifying a mail: " << oldRemoteId << " in the mailbox: " << mailbox << changedProperties;

            auto flags = getFlags(mail);

            const bool messageMoved = changedProperties.contains(ApplicationDomain::Mail::Folder::name);
            const bool messageChanged = changedProperties.contains(ApplicationDomain::Mail::MimeMessage::name);
            if (messageChanged || messageMoved) {
                const auto folderId = folderIdFromMailRid(oldRemoteId);
                const QString oldMailbox = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, folderId);
                const auto content = ensureCRLF(mail.getMimeMessage());
                if (!validateContent(content)) {
                    SinkError() << "Validation failed during modification replay " << mail.identifier() << "\n  Content:" << content;
                    //We can't recover from this other than deleting the mail, so we skip it.
                    return KAsync::null<QByteArray>();
                }
                const QDateTime internalDate = mail.getDate();
                SinkTrace() << "Replacing message. Old mailbox: " << oldMailbox << "New mailbox: " << mailbox << "Flags: " << flags << "Content: " << content;
                KIMAP2::ImapSet set;
                set.add(uid);
                job = login.then(imap->append(mailbox, content, flags, internalDate))
                    .addToContext(imap)
                    .then([=](qint64 uid) {
                        const auto remoteId = assembleMailRid(mail, uid);
                        SinkTrace() << "Finished creating a modified mail: " << remoteId;
                        return imap->remove(oldMailbox, set).then(KAsync::value(remoteId));
                    });
            } else {
                SinkTrace() << "Updating flags only.";
                KIMAP2::ImapSet set;
                set.add(uid);
                job = login.then(imap->select(mailbox))
                    .addToContext(imap)
                    .then(imap->storeFlags(set, flags))
                    .then([=] {
                        SinkTrace() << "Finished modifying mail";
                        return oldRemoteId;
                    });
            }
        }
        return job
            .then([=] (const KAsync::Error &error, const QByteArray &remoteId) {
                if (error) {
                    SinkWarning() << "Error during changereplay: " << error.errorMessage;
                    return imap->logout()
                        .then(KAsync::error<QByteArray>(getError(error)));
                }
                return imap->logout()
                    .then(KAsync::value(remoteId));
            });
    }

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Folder &folder, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        if (operation != Sink::Operation_Creation) {
            if(oldRemoteId.isEmpty()) {
                Q_ASSERT(false);
                return KAsync::error<QByteArray>("Tried to replay modification without old remoteId.");
            }
        }
        auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode, mAuthenticationMode, &mSessionCache);
        auto login = imap->login(mUser, secret());
        KAsync::Job<QByteArray> job = KAsync::null<QByteArray>();
        if (operation == Sink::Operation_Creation) {
            QString parentFolder;
            if (!folder.getParent().isEmpty()) {
                parentFolder = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, folder.getParent());
            }
            SinkTraceCtx(mLogCtx) << "Creating a new folder: " << parentFolder << folder.getName();
            auto rid = QSharedPointer<QByteArray>::create();
            auto createFolder = login.then(imap->createSubfolder(parentFolder, folder.getName()))
                .then([this, imap, rid](const QString &createdFolder) {
                    SinkTraceCtx(mLogCtx) << "Finished creating a new folder: " << createdFolder;
                    *rid = createdFolder.toUtf8();
                });
            if (folder.getSpecialPurpose().isEmpty()) {
                job = createFolder
                    .then([rid](){
                        return *rid;
                    });
            } else { //We try to merge special purpose folders first
                auto specialPurposeFolders = QSharedPointer<QHash<QByteArray, QString>>::create();
                auto mergeJob = imap->login(mUser, secret())
                    .then(imap->fetchFolders([=](const Imap::Folder &folder) {
                        if (SpecialPurpose::isSpecialPurposeFolderName(folder.name())) {
                            specialPurposeFolders->insert(SpecialPurpose::getSpecialPurposeType(folder.name()), folder.path());
                        };
                    }))
                    .then([this, specialPurposeFolders, folder, imap, parentFolder, rid]() -> KAsync::Job<void> {
                        for (const auto &purpose : folder.getSpecialPurpose()) {
                            if (specialPurposeFolders->contains(purpose)) {
                                auto f = specialPurposeFolders->value(purpose);
                                SinkTraceCtx(mLogCtx) << "Merging specialpurpose folder with: " << f << " with purpose: " << purpose;
                                *rid = f.toUtf8();
                                return KAsync::null<void>();
                            }
                        }
                        SinkTraceCtx(mLogCtx) << "No match found for merging, creating a new folder";
                        return imap->createSubfolder(parentFolder, folder.getName())
                            .then([this, imap, rid](const QString &createdFolder) {
                                SinkTraceCtx(mLogCtx) << "Finished creating a new folder: " << createdFolder;
                                *rid = createdFolder.toUtf8();
                            });

                    })
                .then([rid](){
                    return *rid;
                });
                job = mergeJob;
            }
        } else if (operation == Sink::Operation_Removal) {
            SinkTraceCtx(mLogCtx) << "Removing a folder: " << oldRemoteId;
            job = login.then(imap->remove(oldRemoteId))
                .then([this, oldRemoteId, imap] {
                    SinkTraceCtx(mLogCtx) << "Finished removing a folder: " << oldRemoteId;
                    return QByteArray();
                });
        } else if (operation == Sink::Operation_Modification) {
            SinkTraceCtx(mLogCtx) << "Modifying a folder: " << oldRemoteId << folder.getName();
            if (changedProperties.contains(ApplicationDomain::Folder::Name::name)) {
                auto rid = QSharedPointer<QByteArray>::create();
                job = login.then(imap->renameSubfolder(oldRemoteId, folder.getName()))
                    .then([this, imap, rid](const QString &createdFolder) {
                        SinkTraceCtx(mLogCtx) << "Finished renaming a folder: " << createdFolder;
                        *rid = createdFolder.toUtf8();
                    })
                    .then([rid] {
                        return *rid;
                    });
            }
        }
        return job
            .then([=] (const KAsync::Error &error, const QByteArray &remoteId) {
                if (error) {
                    SinkWarning() << "Error during changereplay: " << error.errorMessage;
                    return imap->logout()
                        .then(KAsync::error<QByteArray>(getError(error)));
                }
                return imap->logout()
                    .then(KAsync::value(remoteId));
            });
    }

public:
    QString mServer;
    int mPort;
    Imap::EncryptionMode mEncryptionMode = Imap::NoEncryption;
    Imap::AuthenticationMode mAuthenticationMode;
    QString mUser;
    int mDaysToSync = 0;
    QByteArray mResourceInstanceIdentifier;
    Imap::SessionCache mSessionCache;
};

class ImapInspector : public Sink::Inspector {
public:
    ImapInspector(const Sink::ResourceContext &resourceContext)
        : Sink::Inspector(resourceContext)
    {

    }

protected:
    KAsync::Job<void> inspect(int inspectionType, const QByteArray &inspectionId, const QByteArray &domainType, const QByteArray &entityId, const QByteArray &property, const QVariant &expectedValue) Q_DECL_OVERRIDE {


        if (inspectionType == Sink::ResourceControl::Inspection::ConnectionInspectionType) {
            SinkLog() << "Checking the connection ";
            auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode, mAuthenticationMode);
            return imap->login(mUser, secret())
                .addToContext(imap)
                .then([] {
                    SinkLog() << "Login successful.";
                })
                .then(imap->fetchFolders([=](const Imap::Folder &f) {
                    SinkLog() << "Found a folder " << f.path();
                }))
                .then(imap->logout());
        }

        auto synchronizationStore = QSharedPointer<Sink::Storage::DataStore>::create(Sink::storageLocation(), mResourceContext.instanceId() + ".synchronization", Sink::Storage::DataStore::ReadOnly);
        auto synchronizationTransaction = synchronizationStore->createTransaction(Sink::Storage::DataStore::ReadOnly);

        auto mainStore = QSharedPointer<Sink::Storage::DataStore>::create(Sink::storageLocation(), mResourceContext.instanceId(), Sink::Storage::DataStore::ReadOnly);
        auto transaction = mainStore->createTransaction(Sink::Storage::DataStore::ReadOnly);

        Sink::Storage::EntityStore entityStore(mResourceContext, {"imapresource"});
        auto syncStore = QSharedPointer<Sink::SynchronizerStore>::create(synchronizationTransaction);

        SinkTrace() << "Inspecting " << inspectionType << domainType << entityId << property << expectedValue;

        if (domainType == ENTITY_TYPE_MAIL) {
            const auto mail = entityStore.readLatest<Sink::ApplicationDomain::Mail>(entityId);
            const auto folder = entityStore.readLatest<Sink::ApplicationDomain::Folder>(mail.getFolder());
            const auto folderRemoteId = syncStore->resolveLocalId(ENTITY_TYPE_FOLDER, mail.getFolder());
            const auto mailRemoteId = syncStore->resolveLocalId(ENTITY_TYPE_MAIL, mail.identifier());
            if (mailRemoteId.isEmpty() || folderRemoteId.isEmpty()) {
                //There is no remote id to find if we expect the message to not exist
                if (inspectionType == Sink::ResourceControl::Inspection::ExistenceInspectionType && !expectedValue.toBool()) {
                    return KAsync::null<void>();
                }
                SinkWarning() << "Missing remote id for folder or mail. " << mailRemoteId << folderRemoteId;
                return KAsync::error<void>();
            }
            const auto uid = uidFromMailRid(mailRemoteId);
            SinkTrace() << "Mail remote id: " << folderRemoteId << mailRemoteId << mail.identifier() << folder.identifier();

            KIMAP2::ImapSet set;
            set.add(uid);
            if (set.isEmpty()) {
                return KAsync::error<void>(1, "Couldn't determine uid of mail.");
            }
            KIMAP2::FetchJob::FetchScope scope;
            scope.mode = KIMAP2::FetchJob::FetchScope::Full;
            auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode, mAuthenticationMode);
            auto messageByUid = QSharedPointer<QHash<qint64, Imap::Message>>::create();
            SinkTrace() << "Connecting to:" << mServer << mPort;
            SinkTrace() << "as:" << mUser;
            auto inspectionJob = imap->login(mUser, secret())
                .then(imap->select(folderRemoteId))
                .then([](Imap::SelectResult){})
                .then(imap->fetch(set, scope, [imap, messageByUid](const Imap::Message &message) {
                    //We avoid parsing normally, so we have to do it explicitly here
                    if (message.msg) {
                        message.msg->parse();
                    }
                    messageByUid->insert(message.uid, message);
                }));

            if (inspectionType == Sink::ResourceControl::Inspection::PropertyInspectionType) {
                if (property == "unread") {
                    return inspectionJob.then([=] {
                        auto msg = messageByUid->value(uid);
                        if (expectedValue.toBool() && msg.flags.contains(Imap::Flags::Seen)) {
                            return KAsync::error<void>(1, "Expected unread but couldn't find it.");
                        }
                        if (!expectedValue.toBool() && !msg.flags.contains(Imap::Flags::Seen)) {
                            return KAsync::error<void>(1, "Expected read but couldn't find it.");
                        }
                        return KAsync::null<void>();
                    });
                }
                if (property == "subject") {
                    return inspectionJob.then([=] {
                        auto msg = messageByUid->value(uid);
                        if (msg.msg->subject(true)->asUnicodeString() != expectedValue.toString()) {
                            return KAsync::error<void>(1, "Subject not as expected: " + msg.msg->subject(true)->asUnicodeString());
                        }
                        return KAsync::null<void>();
                    });
                }
            }
            if (inspectionType == Sink::ResourceControl::Inspection::ExistenceInspectionType) {
                return inspectionJob.then([=] {
                    if (!messageByUid->contains(uid)) {
                        SinkWarning() << "Existing messages are: " << messageByUid->keys();
                        SinkWarning() << "We're looking for: " << uid;
                        return KAsync::error<void>(1, "Couldn't find message: " + mailRemoteId);
                    }
                    return KAsync::null<void>();
                });
            }
        }
        if (domainType == ENTITY_TYPE_FOLDER) {
            const auto remoteId = syncStore->resolveLocalId(ENTITY_TYPE_FOLDER, entityId);
            const auto folder = entityStore.readLatest<Sink::ApplicationDomain::Folder>(entityId);

            if (inspectionType == Sink::ResourceControl::Inspection::CacheIntegrityInspectionType) {
                SinkLog() << "Inspecting cache integrity" << remoteId;

                int expectedCount = 0;
                Index index("mail.index.folder", transaction);
                index.lookup(entityId, [&](const QByteArray &sinkId) {
                    expectedCount++;
                    return true;
                },
                [&](const Index::Error &error) {
                    SinkWarning() << "Error in index: " <<  error.message << property;
                });

                auto set = KIMAP2::ImapSet::fromImapSequenceSet("1:*");
                KIMAP2::FetchJob::FetchScope scope;
                scope.mode = KIMAP2::FetchJob::FetchScope::Headers;
                auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode, mAuthenticationMode);
                auto messageByUid = QSharedPointer<QHash<qint64, Imap::Message>>::create();
                return imap->login(mUser, secret())
                    .then(imap->select(remoteId))
                    .then(imap->fetch(set, scope, [=](const Imap::Message message) {
                        messageByUid->insert(message.uid, message);
                    }))
                    .then([imap, messageByUid, expectedCount] {
                        if (messageByUid->size() != expectedCount) {
                            return KAsync::error<void>(1, QString("Wrong number of messages on the server; found %1 instead of %2.").arg(messageByUid->size()).arg(expectedCount));
                        }
                        return KAsync::null<void>();
                    });
            }
            if (inspectionType == Sink::ResourceControl::Inspection::ExistenceInspectionType) {
                auto  folderByPath = QSharedPointer<QSet<QString>>::create();
                auto  folderByName = QSharedPointer<QSet<QString>>::create();

                auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode, mAuthenticationMode);
                auto inspectionJob = imap->login(mUser, secret())
                    .then(imap->fetchFolders([=](const Imap::Folder &f) {
                        *folderByPath << f.path();
                        *folderByName << f.name();
                    }))
                    .then([folderByName, folderByPath, folder, remoteId, imap] {
                        if (!folderByName->contains(folder.getName())) {
                            SinkWarning() << "Existing folders are: " << *folderByPath;
                            SinkWarning() << "We're looking for: " << folder.getName();
                            return KAsync::error<void>(1, "Wrong folder name: " + remoteId);
                        }
                        return KAsync::null<void>();
                    });

                return inspectionJob;
            }

        }
        return KAsync::null<void>();
    }

public:
    QString mServer;
    int mPort;
    Imap::EncryptionMode mEncryptionMode = Imap::NoEncryption;
    Imap::AuthenticationMode mAuthenticationMode;
    QString mUser;
};

class FolderCleanupPreprocessor : public Sink::Preprocessor
{
public:
    void deletedEntity(const ApplicationDomain::ApplicationDomainType &oldEntity) override
    {
        //Remove all mails of a folder when removing the folder.
        const auto revision = entityStore().maxRevision();
        entityStore().indexLookup<ApplicationDomain::Mail, ApplicationDomain::Mail::Folder>(oldEntity.identifier(), [&] (const QByteArray &identifier) {
            deleteEntity(ApplicationDomain::ApplicationDomainType{{}, identifier, revision, {}}, ApplicationDomain::getTypeName<ApplicationDomain::Mail>(), false);
        });
    }
};

ImapResource::ImapResource(const ResourceContext &resourceContext)
    : Sink::GenericResource(resourceContext)
{
    auto config = ResourceConfig::getConfiguration(resourceContext.instanceId());
    auto server = config.value("server").toString();
    auto port = config.value("port").toInt();
    auto user = config.value("username").toString();
    auto daysToSync = config.value("daysToSync", 14).toInt();
    auto starttls = config.value("starttls", false).toBool();
    auto auth = config.value("authenticationMode", "PLAIN").toString();

    auto encryption = Imap::NoEncryption;
    if (server.startsWith("imaps")) {
        encryption = Imap::Tls;
    }
    if (starttls) {
        encryption = Imap::Starttls;
    }

    if (server.startsWith("imap")) {
        server.remove("imap://");
        server.remove("imaps://");
    }
    if (server.contains(':')) {
        auto list = server.split(':');
        server = list.at(0);
        port = list.at(1).toInt();
    }

    //Backwards compatibilty
    //For kolabnow we assumed that port 143 means starttls
    if (encryption == Imap::Tls && port == 143) {
        encryption = Imap::Starttls;
    }

    if (!QSslSocket::supportsSsl()) {
        SinkWarning() << "Qt doesn't support ssl. This is likely a distribution/packaging problem.";
        //On windows this means that the required ssl dll's are missing
        SinkWarning() << "Ssl Library Build Version Number: " << QSslSocket::sslLibraryBuildVersionString();
        SinkWarning() << "Ssl Library Runtime Version Number: " << QSslSocket::sslLibraryVersionString();
    } else {
        SinkTrace() << "Ssl support available";
        SinkTrace() << "Ssl Library Build Version Number: " << QSslSocket::sslLibraryBuildVersionString();
        SinkTrace() << "Ssl Library Runtime Version Number: " << QSslSocket::sslLibraryVersionString();
    }

    auto synchronizer = QSharedPointer<ImapSynchronizer>::create(resourceContext);
    synchronizer->mServer = server;
    synchronizer->mPort = port;
    synchronizer->mEncryptionMode = encryption;
    synchronizer->mAuthenticationMode = Imap::fromAuthString(auth);
    synchronizer->mUser = user;
    synchronizer->mDaysToSync = daysToSync;
    setupSynchronizer(synchronizer);

    auto inspector = QSharedPointer<ImapInspector>::create(resourceContext);
    inspector->mServer = server;
    inspector->mPort = port;
    inspector->mEncryptionMode = encryption;
    inspector->mAuthenticationMode = Imap::fromAuthString(auth);
    inspector->mUser = user;
    setupInspector(inspector);

    setupPreprocessors(ENTITY_TYPE_MAIL, {new SpecialPurposeProcessor, new MailPropertyExtractor});
    setupPreprocessors(ENTITY_TYPE_FOLDER, {new FolderCleanupPreprocessor});
}

ImapResourceFactory::ImapResourceFactory(QObject *parent)
    : Sink::ResourceFactory(parent,
            {Sink::ApplicationDomain::ResourceCapabilities::Mail::mail,
            Sink::ApplicationDomain::ResourceCapabilities::Mail::folder,
            Sink::ApplicationDomain::ResourceCapabilities::Mail::storage,
            Sink::ApplicationDomain::ResourceCapabilities::Mail::drafts,
            Sink::ApplicationDomain::ResourceCapabilities::Mail::folderhierarchy,
            Sink::ApplicationDomain::ResourceCapabilities::Mail::trash,
            Sink::ApplicationDomain::ResourceCapabilities::Mail::sent}
            )
{

}

Sink::Resource *ImapResourceFactory::createResource(const ResourceContext &context)
{
    return new ImapResource(context);
}

void ImapResourceFactory::registerFacades(const QByteArray &name, Sink::FacadeFactory &factory)
{
    factory.registerFacade<ApplicationDomain::Mail, DefaultFacade<ApplicationDomain::Mail>>(name);
    factory.registerFacade<ApplicationDomain::Folder, DefaultFacade<ApplicationDomain::Folder>>(name);
}

void ImapResourceFactory::registerAdaptorFactories(const QByteArray &name, Sink::AdaptorFactoryRegistry &registry)
{
    registry.registerFactory<ApplicationDomain::Mail, DefaultAdaptorFactory<ApplicationDomain::Mail>>(name);
    registry.registerFactory<ApplicationDomain::Folder, DefaultAdaptorFactory<ApplicationDomain::Folder>>(name);
}

void ImapResourceFactory::removeDataFromDisk(const QByteArray &instanceIdentifier)
{
    ImapResource::removeFromDisk(instanceIdentifier);
}

#include "imapresource.moc"
