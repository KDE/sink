/*
 *   Copyright (C) 2018 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include <QTest>
#include <QTcpSocket>

#include <KDAV2/DavCollectionsFetchJob>
#include <KDAV2/DavCollectionCreateJob>
#include <KDAV2/DavCollectionDeleteJob>
#include <KDAV2/DavItemFetchJob>
#include <KDAV2/DavItemModifyJob>
#include <KDAV2/DavItemCreateJob>
#include <KDAV2/DavItemsListJob>

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>

#include "../caldavresource.h"

#include "common/test.h"
#include "common/domain/applicationdomaintype.h"
#include "common/store.h"
#include "common/resourcecontrol.h"
#include "common/secretstore.h"

#include <tests/hawd/dataset.h>
#include <tests/hawd/formatter.h>

using namespace Sink;
using namespace Sink::ApplicationDomain;

/**
 * Test of complete system using the caldav resource.
 *
 * This test requires the caldav resource installed.
 */
class CalDavSyncBenchmark : public QObject
{
    Q_OBJECT

    void resetTestEnvironment()
    {
        system("populatecalendar.sh");
    }

    const QString baseUrl = "http://localhost/dav/calendars/user/doe";
    const QString username = "doe";
    const QString password = "doe";

    Sink::ApplicationDomain::SinkResource createResource()
    {
        auto resource = ApplicationDomain::CalDavResource::create("account1");
        resource.setProperty("server", baseUrl);
        resource.setProperty("username", username);
        Sink::SecretStore::instance().insert(resource.identifier(), password);
        return resource;
    }

    void removeResourceFromDisk(const QByteArray &identifier)
    {
        ::CalDavResource::removeFromDisk(identifier);
    }

    void createEvents(const QString &subject, const QString &collectionName, int num)
    {
        QUrl mainUrl{baseUrl};
        mainUrl.setUserName(username);
        mainUrl.setPassword(password);

        KDAV2::DavUrl davUrl(mainUrl, KDAV2::CalDav);

        auto *job = new KDAV2::DavCollectionsFetchJob(davUrl);
        job->exec();

        const auto collectionUrl = [&] {
            for (const auto &col : job->collections()) {
                qWarning() << "Looking for " << collectionName << col.displayName();
                if (col.displayName() == collectionName) {
                    return col.url().url();
                }
            }
            return QUrl{};
        }();
        Q_ASSERT(!collectionUrl.isEmpty());


        for (int i = 0; i < num; i++) {
            QUrl url{collectionUrl.toString() + subject + QString::number(i) + ".ical"};
            url.setUserInfo(mainUrl.userInfo());

            KDAV2::DavUrl testItemUrl(url, KDAV2::CardDav);
            auto event = QSharedPointer<KCalendarCore::Event>::create();
            event->setSummary(subject);
            event->setDtStart(QDateTime::currentDateTime());
            event->setDtEnd(QDateTime::currentDateTime().addSecs(3600));
            event->setCreated(QDateTime::currentDateTime());
            event->setUid(subject + QString::number(i));
            KDAV2::DavItem item(testItemUrl, QStringLiteral("text/calendar"), KCalendarCore::ICalFormat().toICalString(event).toUtf8(), QString());
            auto createJob = new KDAV2::DavItemCreateJob(item);
            createJob->exec();
            if (createJob->error()) {
                qWarning() << createJob->errorString();
            }
            Q_ASSERT(!createJob->error());
        }
    }

    QByteArray mResourceInstanceIdentifier;
    HAWD::State mHawdState;

private slots:

    void initTestCase()
    {
        Test::initTest();
        resetTestEnvironment();
        auto resource = createResource();
        QVERIFY(!resource.identifier().isEmpty());

        VERIFYEXEC(Store::create(resource));

        mResourceInstanceIdentifier = resource.identifier();
    }

    void cleanup()
    {
        VERIFYEXEC(ResourceControl::shutdown(mResourceInstanceIdentifier));
        removeResourceFromDisk(mResourceInstanceIdentifier);
    }

    void init()
    {
        createEvents("test", "personal", 100);
        VERIFYEXEC(ResourceControl::start(mResourceInstanceIdentifier));
    }

    void testSync()
    {
        Sink::Query query;
        query.resourceFilter(mResourceInstanceIdentifier);

        QTime time;
        time.start();

        // Ensure all local data is processed
        VERIFYEXEC(Store::synchronize(query));
        auto sync = time.elapsed();
        SinkLog() << "Sync took: " << Sink::Log::TraceTime(sync);

        VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
        auto total = time.elapsed();
        SinkLog() << "Total took: " << Sink::Log::TraceTime(total);

        time.start();

        VERIFYEXEC(Store::synchronize(query));
        auto resync = time.elapsed();
        SinkLog() << "ReSync took: " << Sink::Log::TraceTime(resync);

        VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
        auto resynctotal = time.elapsed();
        SinkLog() << "Total resync took: " << Sink::Log::TraceTime(resynctotal);

        // HAWD::Dataset dataset("caldav_sync", mHawdState);
        // HAWD::Dataset::Row row = dataset.row();
        // row.setValue("sync", sync);
        // row.setValue("total", total);
        // row.setValue("resync", resync);
        // row.setValue("resynctotal", resynctotal);
        // dataset.insertRow(row);
        // HAWD::Formatter::print(dataset);
    }
};

QTEST_MAIN(CalDavSyncBenchmark)

#include "caldavsyncbenchmark.moc"
