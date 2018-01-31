/*
 *   Copyright (C) 2016 Christian Mollekopf <chrigi_1@fastmail.fm>
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
#include <QtTest>
#include <QTcpSocket>

#include "../imapresource.h"
#include "../imapserverproxy.h"
#include "tests/testutils.h"

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
 * Test of complete system using the imap resource.
 *
 * This test requires the imap resource installed.
 */
class ImapMailSyncBenchmark : public QObject
{
    Q_OBJECT

    bool isBackendAvailable()
    {
        QTcpSocket socket;
        socket.connectToHost("localhost", 993);
        return socket.waitForConnected(200);
    }

    void resetTestEnvironment()
    {
        system("populatemailbox.sh");
    }

    Sink::ApplicationDomain::SinkResource createResource()
    {
        auto resource = ApplicationDomain::ImapResource::create("account1");
        resource.setProperty("server", "localhost");
        resource.setProperty("port", 143);
        resource.setProperty("username", "doe");
        Sink::SecretStore::instance().insert(resource.identifier(), "doe");
        return resource;
    }

    void removeResourceFromDisk(const QByteArray &identifier)
    {
        ::ImapResource::removeFromDisk(identifier);
    }

    QByteArray mResourceInstanceIdentifier;
    QByteArrayList mCapabilities;
    HAWD::State mHawdState;

private slots:

    void initTestCase()
    {
        Test::initTest();
        QVERIFY(isBackendAvailable());
        resetTestEnvironment();
        auto resource = createResource();
        QVERIFY(!resource.identifier().isEmpty());

        VERIFYEXEC(Store::create(resource));

        mResourceInstanceIdentifier = resource.identifier();
        mCapabilities = resource.getProperty("capabilities").value<QByteArrayList>();
    }

    void cleanup()
    {
        VERIFYEXEC(ResourceControl::shutdown(mResourceInstanceIdentifier));
        removeResourceFromDisk(mResourceInstanceIdentifier);
    }

    void init()
    {
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

        HAWD::Dataset dataset("imap_mail_sync", mHawdState);
        HAWD::Dataset::Row row = dataset.row();
        row.setValue("sync", sync);
        row.setValue("total", total);
        row.setValue("resync", resync);
        row.setValue("resynctotal", resynctotal);
        dataset.insertRow(row);
        HAWD::Formatter::print(dataset);
    }
};

QTEST_MAIN(ImapMailSyncBenchmark)

#include "imapmailsyncbenchmark.moc"
