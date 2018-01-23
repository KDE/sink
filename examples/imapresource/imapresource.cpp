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

#include <QtGlobal>
#include <QDate>
#include <QDateTime>
#include <QtAlgorithms>

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
        folder.setEnabled(f.subscribed);
        auto specialPurpose = [&] {
            if (hasSpecialPurposeFlag(f.flags)) {
                return getSpecialPurposeType(f.flags);
            } else if (SpecialPurpose::isSpecialPurposeFolderName(f.name()) && isToplevel) {
                return SpecialPurpose::getSpecialPurposeType(f.name());
            }
            return QByteArray{};
        }();
        if (!specialPurpose.isEmpty()) {
            folder.setSpecialPurpose(QByteArrayList() << specialPurpose);
        }

        if (!isToplevel) {
            folder.setParent(syncStore().resolveRemoteId(ApplicationDomain::Folder::name, parentFolderRid));
        }
        createOrModify(ApplicationDomain::getTypeName<ApplicationDomain::Folder>(), remoteId, folder);
        return remoteId;
    }

    void synchronizeFolders(const QVector<Folder> &folderList)
    {
        SinkTraceCtx(mLogCtx) << "Found folders " << folderList.size();

        scanForRemovals(ENTITY_TYPE_FOLDER,
            [&folderList](const QByteArray &remoteId) -> bool {
                // folderList.contains(remoteId)
                for (const auto &folder : folderList) {
                    if (folderRid(folder) == remoteId) {
                        return true;
                    }
                }
                return false;
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

    KIMAP2::MessageFlags getFlags(const Sink::ApplicationDomain::Mail &mail)
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

    void synchronizeMails(const QByteArray &folderRid, const QByteArray &folderLocalId, const Message &message)
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

        int count = 0;

        scanForRemovals(ENTITY_TYPE_MAIL,
            [&](const std::function<void(const QByteArray &)> &callback) {
                store().indexLookup<ApplicationDomain::Mail, ApplicationDomain::Mail::Folder>(folderLocalId, callback);
            },
            [&](const QByteArray &remoteId) -> bool {
                if (messages.contains(uidFromMailRid(remoteId))) {
                    return true;
                }
                count++;
                return false;
            }
        );

        const auto elapsed = time->elapsed();
        SinkLog() << "Removed " << count << " mails in " << folderRid << Sink::Log::TraceTime(elapsed) << " " << elapsed/qMax(count, 1) << " [ms/mail]";
    }

    KAsync::Job<void> synchronizeFolder(QSharedPointer<ImapServerProxy> imap, const Imap::Folder &folder, const QDate &dateFilter, bool fetchHeaderAlso = false)
    {
        SinkLogCtx(mLogCtx) << "Synchronizing mails: " << folderRid(folder);
        SinkLogCtx(mLogCtx) << " fetching headers also: " << fetchHeaderAlso;
        const auto folderRemoteId = folderRid(folder);
        if (folder.path().isEmpty() || folderRemoteId.isEmpty()) {
            SinkWarningCtx(mLogCtx) << "Invalid folder " << folderRemoteId << folder.path();
            return KAsync::error<void>("Invalid folder");
        }

        //Start by checking if UIDVALIDITY is still correct
        return KAsync::start([=] {
            bool ok = false;
            const auto uidvalidity = syncStore().readValue(folderRemoteId, "uidvalidity").toLongLong(&ok);
            return imap->select(folder)
                .then([=](const SelectResult &selectResult) {
                    SinkLogCtx(mLogCtx) << "Checking UIDVALIDITY. Local" << uidvalidity << "remote " << selectResult.uidValidity;
                    if (ok && selectResult.uidValidity != uidvalidity) {
                        SinkWarningCtx(mLogCtx) << "UIDVALIDITY changed " << selectResult.uidValidity << uidvalidity;
                        syncStore().removePrefix(folderRemoteId);
                    }
                    syncStore().writeValue(folderRemoteId, "uidvalidity", QByteArray::number(selectResult.uidValidity));
                });
        })
        // //First we fetch flag changes for all messages. Since we don't know which messages are locally available we just get everything and only apply to what we have.
        .then([=] {
            auto uidNext = syncStore().readValue(folderRemoteId, "uidnext").toLongLong();
            bool ok = false;
            const auto changedsince = syncStore().readValue(folderRemoteId, "changedsince").toLongLong(&ok);
            SinkLogCtx(mLogCtx) << "About to update flags" << folder.path() << "changedsince: " << changedsince;
            //If we have any mails so far we start off by updating any changed flags using changedsince
            if (ok) {
                return imap->fetchFlags(folder, KIMAP2::ImapSet(1, qMax(uidNext, qint64(1))), changedsince, [=](const Message &message) {
                    const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, folderRemoteId);
                    const auto remoteId = assembleMailRid(folderLocalId, message.uid);

                    SinkLogCtx(mLogCtx) << "Updating mail flags " << remoteId << message.flags;

                    auto mail = Sink::ApplicationDomain::Mail::create(mResourceInstanceIdentifier);
                    setFlags(mail, message.flags);

                    modify(ENTITY_TYPE_MAIL, remoteId, mail);
                })
                .then([=](const SelectResult &selectResult) {
                    SinkLogCtx(mLogCtx) << "Flags updated. New changedsince value: " << selectResult.highestModSequence;
                    syncStore().writeValue(folderRemoteId, "changedsince", QByteArray::number(selectResult.highestModSequence));
                });
            } else {
                //We hit this path on initial sync and simply record the current changedsince value
                return imap->select(imap->mailboxFromFolder(folder))
                .then([=](const SelectResult &selectResult) {
                    SinkLogCtx(mLogCtx) << "No flags to update. New changedsince value: " << selectResult.highestModSequence;
                    syncStore().writeValue(folderRemoteId, "changedsince", QByteArray::number(selectResult.highestModSequence));
                });
            }
        })
        //Next we synchronize the full set that is given by the date limit.
        //We fetch all data for this set.
        //This will also pull in any new messages in subsequent runs.
        .then([=] {
            auto job = [&] {
                if (dateFilter.isValid()) {
                    SinkLogCtx(mLogCtx) << "Fetching messages since: " << dateFilter;
                    return imap->fetchUidsSince(imap->mailboxFromFolder(folder), dateFilter);
                } else {
                    SinkLogCtx(mLogCtx) << "Fetching messages.";
                    return imap->fetchUids(imap->mailboxFromFolder(folder));
                }
            }();
            return job.then([=](const QVector<qint64> &uidsToFetch) {
                SinkTraceCtx(mLogCtx) << "Received result set " << uidsToFetch;
                SinkTraceCtx(mLogCtx) << "About to fetch mail" << folder.path();
                const auto uidNext = syncStore().readValue(folderRemoteId, "uidnext").toLongLong();

                //Make sure the uids are sorted in reverse order and drop everything below uidNext (so we don't refetch what we already have
                QVector<qint64> filteredAndSorted = uidsToFetch;
                qSort(filteredAndSorted.begin(), filteredAndSorted.end(), qGreater<qint64>());
                auto lowerBound = qLowerBound(filteredAndSorted.begin(), filteredAndSorted.end(), uidNext, qGreater<qint64>());
                if (lowerBound != filteredAndSorted.end()) {
                    filteredAndSorted.erase(lowerBound, filteredAndSorted.end());
                }
                const qint64 lowerBoundUid = filteredAndSorted.isEmpty() ? 0 : filteredAndSorted.last();

                auto maxUid = QSharedPointer<qint64>::create(0);
                if (!filteredAndSorted.isEmpty()) {
                    *maxUid = filteredAndSorted.first();
                }
                SinkTraceCtx(mLogCtx) << "Uids to fetch: " << filteredAndSorted;

                bool headersOnly = false;
                const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, folderRemoteId);
                return imap->fetchMessages(folder, filteredAndSorted, headersOnly, [=](const Message &m) {
                    if (*maxUid < m.uid) {
                        *maxUid = m.uid;
                    }
                    synchronizeMails(folderRemoteId, folderLocalId, m);
                },
                [=](int progress, int total) {
                    reportProgress(progress, total, QByteArrayList{} << folderLocalId);
                    //commit every 10 messages
                    if ((progress % 10) == 0) {
                        commit();
                    }
                })
                .then([=] {
                    SinkLogCtx(mLogCtx) << "UIDMAX: " << *maxUid << folder.path();
                    if (*maxUid > 0) {
                        syncStore().writeValue(folderRemoteId, "uidnext", QByteArray::number(*maxUid));
                    }
                    syncStore().writeValue(folderRemoteId, "fullsetLowerbound", QByteArray::number(lowerBoundUid));
                    commit();
                });
            });
        })
        .then<void>([=] {
            bool ok = false;
            const auto headersFetched = !syncStore().readValue(folderRemoteId, "headersFetched").isEmpty();
            const auto fullsetLowerbound = syncStore().readValue(folderRemoteId, "fullsetLowerbound").toLongLong(&ok);
            if (ok && !headersFetched) {
                SinkLogCtx(mLogCtx) << "Fetching headers until: " << fullsetLowerbound;

                return imap->fetchUids(imap->mailboxFromFolder(folder))
                .then([=] (const QVector<qint64> &uids) {
                    //sort in reverse order and remove everything greater than fullsetLowerbound
                    QVector<qint64> toFetch = uids;
                    qSort(toFetch.begin(), toFetch.end(), qGreater<qint64>());
                    if (fullsetLowerbound) {
                        auto upperBound = qUpperBound(toFetch.begin(), toFetch.end(), fullsetLowerbound, qGreater<qint64>());
                        if (upperBound != toFetch.begin()) {
                            toFetch.erase(toFetch.begin(), upperBound);
                        }
                    }
                    SinkLogCtx(mLogCtx) << "Fetching headers for: " << toFetch;

                    bool headersOnly = true;
                    const auto folderLocalId = syncStore().resolveRemoteId(ENTITY_TYPE_FOLDER, folderRemoteId);
                    return imap->fetchMessages(folder, toFetch, headersOnly, [=](const Message &m) {
                        synchronizeMails(folderRemoteId, folderLocalId, m);
                    },
                    [=](int progress, int total) {
                        reportProgress(progress, total, QByteArrayList{} << folderLocalId);
                        //commit every 100 messages
                        if ((progress % 100) == 0) {
                            commit();
                        }
                    });
                })
                .then([=] {
                    SinkLogCtx(mLogCtx) << "Headers fetched: " << folder.path();
                    syncStore().writeValue(folderRemoteId, "headersFetched", "true");
                    commit();
                });

            } else {
                SinkLogCtx(mLogCtx) << "No additional headers to fetch.";
            }
            return KAsync::null();
        })
        //Finally remove messages that are no longer existing on the server.
        .then([=] {
            //TODO do an examine with QRESYNC and remove VANISHED messages if supported instead
            return imap->fetchUids(folder).then([=](const QVector<qint64> &uids) {
                SinkTraceCtx(mLogCtx) << "Syncing removals: " << folder.path();
                synchronizeRemovals(folderRemoteId, uids.toList().toSet());
                commit();
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

    KAsync::Job<void> login(QSharedPointer<ImapServerProxy> imap)
    {
        SinkTrace() << "Connecting to:" << mServer << mPort;
        SinkTrace() << "as:" << mUser;
        return imap->login(mUser, secret())
        .addToContext(imap);
    }

    KAsync::Job<QVector<Folder>> getFolderList(QSharedPointer<ImapServerProxy> imap, const Sink::QueryBase &query)
    {
        if (query.hasFilter<ApplicationDomain::Mail::Folder>()) {
            //If we have a folder filter fetch full payload of date-range & all headers
            QVector<Folder> folders;
            auto folderFilter = query.getFilter<ApplicationDomain::Mail::Folder>();
            auto localIds = resolveFilter(folderFilter);
            auto folderRemoteIds = syncStore().resolveLocalIds(ApplicationDomain::getTypeName<ApplicationDomain::Folder>(), localIds);
            for (const auto &r : folderRemoteIds) {
                folders << Folder{r};
            }
            return KAsync::value(folders);
        } else {
            //Otherwise fetch full payload for daterange
            auto folderList = QSharedPointer<QVector<Folder>>::create();
            return  imap->fetchFolders([folderList](const Folder &folder) {
                if (!folder.noselect && folder.subscribed) {
                    *folderList << folder;
                }
            })
            .onError([](const KAsync::Error &error) {
                SinkWarning() << "Folder list sync failed.";
            })
            .then([folderList] { return *folderList; } );
        }
    }

    KAsync::Error getError(const KAsync::Error &error)
    {
        if (error) {
            switch(error.errorCode) {
                case Imap::CouldNotConnectError:
                    return {ApplicationDomain::ConnectionError, error.errorMessage};
                case Imap::SslHandshakeError:
                    return {ApplicationDomain::LoginError, error.errorMessage};
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
        auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode, &mSessionCache);
        if (query.type() == ApplicationDomain::getTypeName<ApplicationDomain::Folder>()) {
            return login(imap)
            .then([=] {
                auto folderList = QSharedPointer<QVector<Folder>>::create();
                return  imap->fetchFolders([folderList](const Folder &folder) {
                    *folderList << folder;
                })
                .then([this, folderList]() {
                    synchronizeFolders(*folderList);
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
                        synchronizeMails(folderRemoteId, folderLocalId, m);
                    },
                    [=](int progress, int total) {
                        reportProgress(progress, total, QByteArrayList{} << folderLocalId);
                        //commit every 100 messages
                        if ((progress % 100) == 0) {
                            commit();
                        }
                    });
                } else {
                    //Otherwise we sync the folder(s)
                    bool syncHeaders = query.hasFilter<ApplicationDomain::Mail::Folder>();
                    //FIXME If we were able to to flush in between we could just query the local store for the folder list.
                    return getFolderList(imap, query)
                        .serialEach([=](const Folder &folder) {
                            SinkLog() << "Syncing folder " << folder.path();
                            //Emit notification that the folder is being synced.
                            //The synchronizer can't do that because it has no concept of the folder filter on a mail sync scope meaning that the folder is being synchronized.
                            QDate dateFilter;
                            auto filter = query.getFilter<ApplicationDomain::Mail::Date>();
                            if (filter.value.canConvert<QDate>()) {
                                dateFilter = filter.value.value<QDate>();
                                SinkLog() << " with date-range " << dateFilter;
                            }
                            return synchronizeFolder(imap, folder, dateFilter, syncHeaders)
                                .onError([=](const KAsync::Error &error) {
                                    SinkWarning() << "Failed to sync folder: " << folder.path() << "Error: " << error.errorMessage;
                                });
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

    KAsync::Job<QByteArray> replay(const ApplicationDomain::Mail &mail, Sink::Operation operation, const QByteArray &oldRemoteId, const QList<QByteArray> &changedProperties) Q_DECL_OVERRIDE
    {
        if (operation != Sink::Operation_Creation) {
            if(oldRemoteId.isEmpty()) {
                // return KAsync::error<QByteArray>("Tried to replay modification without old remoteId.");
                qWarning() << "Tried to replay modification without old remoteId.";
                // Since we can't recover from the situation we just skip over the revision.
                // FIXME figure out how we can ever end up in this situation
                return KAsync::null<QByteArray>();
            }
        }
        auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode, &mSessionCache);
        auto login = imap->login(mUser, secret());
        KAsync::Job<QByteArray> job = KAsync::null<QByteArray>();
        if (operation == Sink::Operation_Creation) {
            const QString mailbox = syncStore().resolveLocalId(ENTITY_TYPE_FOLDER, mail.getFolder());
            const auto content = ensureCRLF(mail.getMimeMessage());
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
        auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode, &mSessionCache);
        auto login = imap->login(mUser, secret());
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
                return createFolder
                    .then([rid](){
                        return *rid;
                    });
            } else { //We try to merge special purpose folders first
                auto  specialPurposeFolders = QSharedPointer<QHash<QByteArray, QString>>::create();
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
                return mergeJob;
            }
        } else if (operation == Sink::Operation_Removal) {
            SinkTraceCtx(mLogCtx) << "Removing a folder: " << oldRemoteId;
            return login.then(imap->remove(oldRemoteId))
                .then([this, oldRemoteId, imap] {
                    SinkTraceCtx(mLogCtx) << "Finished removing a folder: " << oldRemoteId;
                    return QByteArray();
                });
        } else if (operation == Sink::Operation_Modification) {
            SinkTraceCtx(mLogCtx) << "Renaming a folder: " << oldRemoteId << folder.getName();
            auto rid = QSharedPointer<QByteArray>::create();
            return login.then(imap->renameSubfolder(oldRemoteId, folder.getName()))
                .then([this, imap, rid](const QString &createdFolder) {
                    SinkTraceCtx(mLogCtx) << "Finished renaming a folder: " << createdFolder;
                    *rid = createdFolder.toUtf8();
                })
                .then([rid] {
                    return *rid;
                });
        }
        return KAsync::null<QByteArray>();
    }

public:
    QString mServer;
    int mPort;
    Imap::EncryptionMode mEncryptionMode = Imap::NoEncryption;
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
            auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode);
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
                },
                [&](const Index::Error &error) {
                    SinkWarning() << "Error in index: " <<  error.message << property;
                });

                auto set = KIMAP2::ImapSet::fromImapSequenceSet("1:*");
                KIMAP2::FetchJob::FetchScope scope;
                scope.mode = KIMAP2::FetchJob::FetchScope::Headers;
                auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode);
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

                auto imap = QSharedPointer<ImapServerProxy>::create(mServer, mPort, mEncryptionMode);
                auto inspectionJob = imap->login(mUser, secret())
                    .then(imap->fetchFolders([=](const Imap::Folder &f) {
                        *folderByPath << f.path();
                        *folderByName << f.name();
                    }))
                    .then([this, folderByName, folderByPath, folder, remoteId, imap] {
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
    QString mUser;
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

    auto synchronizer = QSharedPointer<ImapSynchronizer>::create(resourceContext);
    synchronizer->mServer = server;
    synchronizer->mPort = port;
    synchronizer->mEncryptionMode = encryption;
    synchronizer->mUser = user;
    synchronizer->mDaysToSync = daysToSync;
    setupSynchronizer(synchronizer);

    auto inspector = QSharedPointer<ImapInspector>::create(resourceContext);
    inspector->mServer = server;
    inspector->mPort = port;
    inspector->mEncryptionMode = encryption;
    inspector->mUser = user;
    setupInspector(inspector);

    setupPreprocessors(ENTITY_TYPE_MAIL, QVector<Sink::Preprocessor*>() << new SpecialPurposeProcessor << new MailPropertyExtractor);
    setupPreprocessors(ENTITY_TYPE_FOLDER, QVector<Sink::Preprocessor*>());
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
