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

#include <common/facade.h>
#include <common/domainadaptor.h>
#include <common/resultprovider.h>
#include <common/synclistresult.h>
#include <common/definitions.h>
#include <common/query.h>
#include <common/store.h>
#include <common/pipeline.h>
#include <common/index.h>
#include <common/indexupdater.h>

#include "hawd/dataset.h"
#include "hawd/formatter.h"

#include <iostream>
#include <math.h>

#include "mail_generated.h"
#include "createentity_generated.h"
#include "getrssusage.h"

/**
 * Benchmark mail query performance.
 */
class MailQueryBenchmark : public QObject
{
    Q_OBJECT

    QByteArray resourceIdentifier;
    HAWD::State mHawdState;

    void populateDatabase(int count)
    {
        TestResource::removeFromDisk(resourceIdentifier);

        auto pipeline = QSharedPointer<Sink::Pipeline>::create(resourceIdentifier);

        auto mailFactory = QSharedPointer<TestMailAdaptorFactory>::create();
        auto indexer = QSharedPointer<DefaultIndexUpdater<Sink::ApplicationDomain::Mail> >::create();

        pipeline->setPreprocessors("mail", QVector<Sink::Preprocessor*>() << indexer.data());
        pipeline->setAdaptorFactory("mail", mailFactory);

        auto domainTypeAdaptorFactory = QSharedPointer<TestMailAdaptorFactory>::create();

        pipeline->startTransaction();
        const auto date = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < count; i++) {
            auto domainObject = Sink::ApplicationDomain::Mail::Ptr::create();
            domainObject->setProperty("uid", "uid");
            domainObject->setProperty("subject", QString("subject%1").arg(i));
            domainObject->setProperty("date", date.addSecs(count));
            domainObject->setProperty("folder", "folder1");
            // domainObject->setProperty("attachment", attachment);
            const auto command = createCommand<Sink::ApplicationDomain::Mail>(*domainObject, *domainTypeAdaptorFactory);
            pipeline->newEntity(command.data(), command.size());
        }
        pipeline->commit();
    }

    void testLoad(const Sink::Query &query, int count)
    {
        const auto startingRss = getCurrentRSS();


        //Benchmark
        QTime time;
        time.start();

        auto resultSet = QSharedPointer<Sink::ResultProvider<Sink::ApplicationDomain::Mail::Ptr> >::create();
        auto resourceAccess = QSharedPointer<TestResourceAccess>::create();
        TestMailResourceFacade facade(resourceIdentifier, resourceAccess);

        auto ret = facade.load(query);
        ret.first.exec().waitForFinished();
        auto emitter = ret.second;
        QList<Sink::ApplicationDomain::Mail::Ptr> list;
        emitter->onAdded([&list](const Sink::ApplicationDomain::Mail::Ptr &mail) {
            list << mail;
        });
        bool done = false;
        emitter->onInitialResultSetComplete([&done](const Sink::ApplicationDomain::Mail::Ptr &mail) {
            done = true;
        });
        emitter->fetch(Sink::ApplicationDomain::Mail::Ptr());
        QTRY_VERIFY(done);
        QCOMPARE(list.size(), query.limit);

        const auto elapsed = time.elapsed();

        const auto finalRss = getCurrentRSS();
        const auto rssGrowth = finalRss - startingRss;
        //Since the database is memory mapped it is attributted to the resident set size.
        const auto rssWithoutDb = finalRss - Sink::Storage(Sink::storageLocation(), resourceIdentifier, Sink::Storage::ReadWrite).diskUsage();
        const auto peakRss =  getPeakRSS();
        //How much peak deviates from final rss in percent (should be around 0)
        const auto percentageRssError = static_cast<double>(peakRss - finalRss)*100.0/static_cast<double>(finalRss);
        auto rssGrowthPerEntity = rssGrowth/count;

        std::cout << "Loaded " << list.size() << " results." << std::endl;
        std::cout << "The query took [ms]: " << elapsed << std::endl;
        std::cout << "Current Rss usage [kb]: " << finalRss/1024 << std::endl;
        std::cout << "Peak Rss usage [kb]: " << peakRss/1024 << std::endl;
        std::cout << "Rss growth [kb]: " << rssGrowth/1024 << std::endl;
        std::cout << "Rss growth per entity [byte]: " << rssGrowthPerEntity << std::endl;
        std::cout << "Rss without db [kb]: " << rssWithoutDb/1024 << std::endl;
        std::cout << "Percentage error: " << percentageRssError << std::endl;

        HAWD::Dataset dataset("mail_query", mHawdState);
        HAWD::Dataset::Row row = dataset.row();
        row.setValue("rows", list.size());
        row.setValue("queryResultPerMs", (qreal)list.size()/elapsed);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);

        QVERIFY(percentageRssError < 10);
        //TODO This is much more than it should it seems, although adding the attachment results in pretty exactly a 1k increase,
        //so it doesn't look like that memory is being duplicated.
        QVERIFY(rssGrowthPerEntity < 3300);

        // Print memory layout, RSS is what is in memory
        // std::system("exec pmap -x \"$PPID\"");
        // std::system("top -p \"$PPID\" -b -n 1");
    }

private slots:

    void init()
    {
        resourceIdentifier = "org.kde.test.instance1";
    }

    void test50k()
    {
        Sink::Query query;
        query.liveQuery = false;
        query.requestedProperties << "uid" << "subject" << "date";
        query.sortProperty = "date";
        query.propertyFilter.insert("folder", "folder1");
        query.limit = 1000;

        populateDatabase(50000);
        testLoad(query, 50000);
    }

};

QTEST_MAIN(MailQueryBenchmark)
#include "mailquerybenchmark.moc"
