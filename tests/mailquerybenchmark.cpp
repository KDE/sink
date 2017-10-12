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

    void populateDatabase(int count, int folderSpreadFactor = 0, bool clear = true, int offset = 0)
    {
        if (clear) {
            TestResource::removeFromDisk(resourceIdentifier);
        }

        Sink::ResourceContext resourceContext{resourceIdentifier, "test", {{"mail", QSharedPointer<TestMailAdaptorFactory>::create()}}};
        Sink::Storage::EntityStore entityStore{resourceContext, {}};
        entityStore.startTransaction(Sink::Storage::DataStore::ReadWrite);

        const auto date = QDateTime::currentDateTimeUtc();
        for (int i = offset; i < offset + count; i++) {
            auto domainObject = Mail::createEntity<Mail>(resourceIdentifier);
            domainObject.setExtractedMessageId("uid");
            domainObject.setExtractedParentMessageId("parentuid");
            domainObject.setExtractedSubject(QString("subject%1").arg(i));
            domainObject.setExtractedDate(date.addSecs(count));
            if (folderSpreadFactor == 0) {
                domainObject.setFolder("folder1");
            } else {
                domainObject.setFolder(QByteArray("folder") + QByteArray::number(i - (i % folderSpreadFactor)));
            }

            entityStore.add("mail", domainObject, false);
        }

        entityStore.commitTransaction();
    }

    qreal testLoad(const Sink::Query &query, int count, int expectedSize)
    {
        const auto startingRss = getCurrentRSS();

        // Benchmark
        QTime time;
        time.start();

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
        while (!done) {
            QTest::qWait(1);
        }
        Q_ASSERT(list.size() == expectedSize);

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

        Q_ASSERT(percentageRssError < 10);
        // TODO This is much more than it should it seems, although adding the attachment results in pretty exactly a 1k increase,
        // so it doesn't look like that memory is being duplicated.
        Q_ASSERT(rssGrowthPerEntity < 3300);

        // Print memory layout, RSS is what is in memory
        // std::system("exec pmap -x \"$PPID\"");
        // std::system("top -p \"$PPID\" -b -n 1");
        return (qreal)list.size() / elapsed;
    }

private slots:

    void init()
    {
        resourceIdentifier = "sink.test.instance1";
    }

    void test50k()
    {
        int count = 50000;
        int limit = 1000;
        qreal simpleResultRate = 0;
        qreal threadResultRate = 0;
        {
            Sink::Query query;
            query.request<Mail::MessageId>()
                .request<Mail::Subject>()
                .request<Mail::Date>();
            query.sort<Mail::Date>();
            query.filter<Mail::Folder>("folder1");
            query.limit(limit);

            populateDatabase(count);
            simpleResultRate = testLoad(query, count, query.limit());
        }
        {
            Sink::Query query;
            query.request<Mail::MessageId>()
                .request<Mail::Subject>()
                .request<Mail::Date>();
            // query.filter<ApplicationDomain::Mail::Trash>(false);
            query.reduce<ApplicationDomain::Mail::Folder>(Query::Reduce::Selector::max<ApplicationDomain::Mail::Date>());
            query.limit(limit);

            int mailsPerFolder = 10;

            populateDatabase(count, mailsPerFolder);
            threadResultRate = testLoad(query, count, query.limit());
        }
        HAWD::Dataset dataset("mail_query", mHawdState);
        HAWD::Dataset::Row row = dataset.row();
        row.setValue("rows", limit);
        row.setValue("simple", simpleResultRate);
        row.setValue("threadleader", threadResultRate);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
    }

    void testIncremental()
    {
        Sink::Query query{Sink::Query::LiveQuery};
        query.request<Mail::MessageId>()
             .request<Mail::Subject>()
             .request<Mail::Date>();
        query.sort<ApplicationDomain::Mail::Date>();
        query.reduce<ApplicationDomain::Mail::Folder>(Query::Reduce::Selector::max<ApplicationDomain::Mail::Date>());
        query.limit(1000);

        int count = 1000;
        populateDatabase(count, 10);
        auto expectedSize = 100;
        QTime time;
        time.start();
        auto domainTypeAdaptorFactory = QSharedPointer<TestMailAdaptorFactory>::create();
        Sink::ResourceContext context{resourceIdentifier, "test", {{"mail", domainTypeAdaptorFactory}}};
        context.mResourceAccess = QSharedPointer<TestResourceAccess>::create();
        TestMailResourceFacade facade(context);

        auto ret = facade.load(query, Sink::Log::Context{"benchmark"});
        ret.first.exec().waitForFinished();
        auto emitter = ret.second;
        QList<Mail::Ptr> added;
        QList<Mail::Ptr> removed;
        QList<Mail::Ptr> modified;
        emitter->onAdded([&](const Mail::Ptr &mail) { added << mail; /*qWarning() << "Added";*/ });
        emitter->onRemoved([&](const Mail::Ptr &mail) { removed << mail; /*qWarning() << "Removed";*/ });
        emitter->onModified([&](const Mail::Ptr &mail) { modified << mail; /*qWarning() << "Modified";*/ });
        bool done = false;
        emitter->onInitialResultSetComplete([&done](const Mail::Ptr &mail, bool) { done = true; });
        emitter->fetch(Mail::Ptr());
        while (!done) {
            QTest::qWait(1);
        }
        QCOMPARE(added.size(), expectedSize);

        std::cout << "Initial query took: " << time.elapsed() << std::endl;

        populateDatabase(count, 10, false, count);
        time.restart();
        for (int i = 0; i <= 10; i++) {
            //Simulate revision updates in steps of 100
            context.mResourceAccess->revisionChanged(1000 + i * 100);
        }
        //We should have 200 items in total in the end. 2000 mails / 10 folders => 200 reduced mails
        QTRY_COMPARE(added.count(), 200);
        //We get one modification per thread from the first 100 (1000 mails / 10 folders), everything else is optimized away because we ignore repeated updates to the same thread.
        QTRY_COMPARE(modified.count(), 100);
        std::cout << "Incremental query took " << time.elapsed() << std::endl;
        std::cout << "added " << added.count() << std::endl;
        std::cout << "modified " << modified.count() << std::endl;
        std::cout << "removed " << removed.count() << std::endl;
    }
};

QTEST_MAIN(MailQueryBenchmark)
#include "mailquerybenchmark.moc"
