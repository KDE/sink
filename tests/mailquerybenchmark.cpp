/*
 * Copyright (C) 2016 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include <QtTest>

#include <QString>

#include "testimplementations.h"

#include <common/resultprovider.h>
#include <common/definitions.h>
#include <common/query.h>
#include <common/storage/entitystore.h>

#include "hawd/dataset.h"
#include "hawd/formatter.h"

#include <iostream>
#include <math.h>

#include "mail_generated.h"
#include "createentity_generated.h"
#include "getrssusage.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

/**
 * Benchmark mail query performance.
 */
class MailQueryBenchmark : public QObject
{
    Q_OBJECT

    QByteArray resourceIdentifier;
    HAWD::State mHawdState;

    void populateDatabase(int count, int folderSpreadFactor = 0)
    {
        TestResource::removeFromDisk(resourceIdentifier);

        Sink::ResourceContext resourceContext{resourceIdentifier, "test", {{"mail", QSharedPointer<TestMailAdaptorFactory>::create()}}};
        Sink::Storage::EntityStore entityStore{resourceContext, {}};
        entityStore.startTransaction(Sink::Storage::DataStore::ReadWrite);

        const auto date = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < count; i++) {
            auto domainObject = Mail::createEntity<Mail>(resourceIdentifier);
            domainObject.setExtractedMessageId("uid");
            domainObject.setExtractedParentMessageId("parentuid");
            domainObject.setExtractedSubject(QString("subject%1").arg(i));
            domainObject.setExtractedDate(date.addSecs(count));
            if (folderSpreadFactor == 0) {
                domainObject.setFolder("folder1");
            } else {
                domainObject.setFolder(QByteArray("folder") + QByteArray::number(i % folderSpreadFactor));
            }

            entityStore.add("mail", domainObject, false, [] (const Mail &) {});
        }

        entityStore.commitTransaction();
    }

    void testLoad(const Sink::Query &query, int count, int expectedSize)
    {
        const auto startingRss = getCurrentRSS();

        // Benchmark
        QTime time;
        time.start();
        auto resultSet = QSharedPointer<Sink::ResultProvider<Mail::Ptr>>::create();

        //FIXME why do we need this here?
        auto domainTypeAdaptorFactory = QSharedPointer<TestMailAdaptorFactory>::create();
        Sink::ResourceContext context{resourceIdentifier, "test", {{"mail", domainTypeAdaptorFactory}}};
        context.mResourceAccess = QSharedPointer<TestResourceAccess>::create();
        TestMailResourceFacade facade(context);

        auto ret = facade.load(query, Sink::Log::Context{"benchmark"});
        ret.first.exec().waitForFinished();
        auto emitter = ret.second;
        QList<Mail::Ptr> list;
        emitter->onAdded([&list](const Mail::Ptr &mail) { list << mail; });
        bool done = false;
        emitter->onInitialResultSetComplete([&done](const Mail::Ptr &mail, bool) { done = true; });
        emitter->fetch(Mail::Ptr());
        QTRY_VERIFY(done);
        QCOMPARE(list.size(), expectedSize);

        const auto elapsed = time.elapsed();

        const auto finalRss = getCurrentRSS();
        const auto rssGrowth = finalRss - startingRss;
        // Since the database is memory mapped it is attributted to the resident set size.
        const auto rssWithoutDb = finalRss - Sink::Storage::DataStore(Sink::storageLocation(), resourceIdentifier, Sink::Storage::DataStore::ReadWrite).diskUsage();
        const auto peakRss = getPeakRSS();
        // How much peak deviates from final rss in percent (should be around 0)
        const auto percentageRssError = static_cast<double>(peakRss - finalRss) * 100.0 / static_cast<double>(finalRss);
        auto rssGrowthPerEntity = rssGrowth / count;

        std::cout << "Loaded " << list.size() << " results." << std::endl;
        std::cout << "The query took [ms]: " << elapsed << std::endl;
        std::cout << "Current Rss usage [kb]: " << finalRss / 1024 << std::endl;
        std::cout << "Peak Rss usage [kb]: " << peakRss / 1024 << std::endl;
        std::cout << "Rss growth [kb]: " << rssGrowth / 1024 << std::endl;
        std::cout << "Rss growth per entity [byte]: " << rssGrowthPerEntity << std::endl;
        std::cout << "Rss without db [kb]: " << rssWithoutDb / 1024 << std::endl;
        std::cout << "Percentage error: " << percentageRssError << std::endl;

        HAWD::Dataset dataset("mail_query", mHawdState);
        HAWD::Dataset::Row row = dataset.row();
        row.setValue("rows", list.size());
        row.setValue("queryResultPerMs", (qreal)list.size() / elapsed);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);

        QVERIFY(percentageRssError < 10);
        // TODO This is much more than it should it seems, although adding the attachment results in pretty exactly a 1k increase,
        // so it doesn't look like that memory is being duplicated.
        QVERIFY(rssGrowthPerEntity < 3300);

        // Print memory layout, RSS is what is in memory
        // std::system("exec pmap -x \"$PPID\"");
        // std::system("top -p \"$PPID\" -b -n 1");
    }

private slots:

    void init()
    {
        resourceIdentifier = "sink.test.instance1";
    }

    void test50k()
    {
        Sink::Query query;
        query.request<Mail::MessageId>()
             .request<Mail::Subject>()
             .request<Mail::Date>();
        query.sort<Mail::Date>();
        query.filter<Mail::Folder>("folder1");
        query.limit(1000);

        populateDatabase(50000);
        testLoad(query, 50000, query.limit());
    }

    void test50kThreadleader()
    {
        Sink::Query query;
        query.request<Mail::MessageId>()
             .request<Mail::Subject>()
             .request<Mail::Date>();
        // query.filter<ApplicationDomain::Mail::Trash>(false);
        query.reduce<ApplicationDomain::Mail::Folder>(Query::Reduce::Selector::max<ApplicationDomain::Mail::Date>());
        query.limit(1000);

        int mailsPerFolder = 100;
        populateDatabase(50000, mailsPerFolder);
        testLoad(query, 50000, mailsPerFolder);
    }
};

QTEST_MAIN(MailQueryBenchmark)
#include "mailquerybenchmark.moc"
