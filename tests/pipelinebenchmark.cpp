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
#include <common/definitions.h>
#include <common/query.h>
#include <common/store.h>
#include <common/pipeline.h>
#include <common/index.h>
#include <common/adaptorfactoryregistry.h>

#include "hawd/dataset.h"
#include "hawd/formatter.h"

#include <iostream>
#include <math.h>

#include "mail_generated.h"
#include "createentity_generated.h"
#include "getrssusage.h"

/**
 * Benchmark pipeline processing speed.
 *
 * This benchmark especially highlights:
 * * Cost of an index in speed and size
 */
class PipelineBenchmark : public QObject
{
    Q_OBJECT

    QByteArray resourceIdentifier;
    HAWD::State mHawdState;

    void populateDatabase(int count, const QVector<Sink::Preprocessor *> &preprocessors)
    {
        TestResource::removeFromDisk(resourceIdentifier);

        auto pipeline = QSharedPointer<Sink::Pipeline>::create(Sink::ResourceContext{resourceIdentifier, "test", Sink::AdaptorFactoryRegistry::instance().getFactories("test")}, "test");
        pipeline->setPreprocessors("mail", preprocessors);

        QTime time;
        time.start();

        auto domainTypeAdaptorFactory = QSharedPointer<TestMailAdaptorFactory>::create();

        pipeline->startTransaction();
        const auto date = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < count; i++) {
            auto domainObject = Sink::ApplicationDomain::Mail::Ptr::create();
            domainObject->setExtractedMessageId("uid");
            domainObject->setExtractedSubject(QString("subject%1").arg(i));
            domainObject->setExtractedDate(date.addSecs(count));
            domainObject->setFolder("folder1");
            // domainObject->setProperty("attachment", attachment);
            const auto command = createCommand<Sink::ApplicationDomain::Mail>(*domainObject, *domainTypeAdaptorFactory);
            pipeline->newEntity(command.data(), command.size()).exec();
        }
        pipeline->commit();
        auto appendTime = time.elapsed();

        auto allProcessedTime = time.elapsed();

        // Print memory layout, RSS is what is in memory
        // std::system("exec pmap -x \"$PPID\"");
        //
        std::cout << "Size: " << Sink::Storage::DataStore(Sink::storageLocation(), resourceIdentifier, Sink::Storage::DataStore::ReadOnly).diskUsage() / 1024 << " [kb]" << std::endl;
        std::cout << "Time: " << allProcessedTime << " [ms]" << std::endl;

        HAWD::Dataset dataset("pipeline", mHawdState);
        HAWD::Dataset::Row row = dataset.row();

        row.setValue("rows", count);
        row.setValue("append", (qreal)count / appendTime);
        row.setValue("total", (qreal)count / allProcessedTime);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
    }

private slots:

    void init()
    {
        Sink::Log::setDebugOutputLevel(Sink::Log::Warning);
        Sink::AdaptorFactoryRegistry::instance().registerFactory<Sink::ApplicationDomain::Mail, TestMailAdaptorFactory>("test");
        resourceIdentifier = "sink.test.instance1";
    }

    void testWithIndex()
    {
        populateDatabase(10000, QVector<Sink::Preprocessor *>());
    }
};

QTEST_MAIN(PipelineBenchmark)
#include "pipelinebenchmark.moc"
