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

#include <tests/mailsynctest.h>
#include "../imapresource.h"
#include "../imapserverproxy.h"

#include "common/test.h"
#include "common/domain/applicationdomaintype.h"
#include "common/store.h"
#include "common/resourcecontrol.h"

using namespace Sink;
using namespace Sink::ApplicationDomain;

SINK_DEBUG_AREA("ImapMailSyncBenchmark")

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
        resource.setProperty("port", 993);
        resource.setProperty("username", "doe");
        resource.setProperty("password", "doe");
        return resource;
    }

    void removeResourceFromDisk(const QByteArray &identifier)
    {
        ::ImapResource::removeFromDisk(identifier);
    }

    QByteArray mResourceInstanceIdentifier;
    QByteArrayList mCapabilities;

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
        //TODO the shutdown job fails if the resource is already shut down
        // VERIFYEXEC(ResourceControl::shutdown(mResourceInstanceIdentifier));
        ResourceControl::shutdown(mResourceInstanceIdentifier).exec().waitForFinished();
        removeResourceFromDisk(mResourceInstanceIdentifier);
    }

    void init()
    {
        qDebug();
        qDebug() << "-----------------------------------------";
        qDebug();
        VERIFYEXEC(ResourceControl::start(mResourceInstanceIdentifier));
    }

    void testSync()
    {
        Sink::Query query;
        query.resources << mResourceInstanceIdentifier;
        query.request<Folder::Name>().request<Folder::SpecialPurpose>();

        QTime time;
        time.start();

        // Ensure all local data is processed
        VERIFYEXEC(Store::synchronize(query));
        SinkLog() << "Sync took: " << Sink::Log::TraceTime(time.elapsed());

        VERIFYEXEC(ResourceControl::flushMessageQueue(query.resources));
        SinkLog() << "Total took: " << Sink::Log::TraceTime(time.elapsed());

        time.start();

        VERIFYEXEC(Store::synchronize(query));
        SinkLog() << "ReSync took: " << Sink::Log::TraceTime(time.elapsed());

        VERIFYEXEC(ResourceControl::flushMessageQueue(query.resources));
        SinkLog() << "Total resync took: " << Sink::Log::TraceTime(time.elapsed());
    }
};

QTEST_MAIN(ImapMailSyncBenchmark)

#include "imapmailsyncbenchmark.moc"
