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
#include "common/commands.h"

#include <tests/hawd/dataset.h>
#include <tests/hawd/formatter.h>

using namespace Sink;
using namespace Sink::ApplicationDomain;

/**
 *  Test if the system remains somewhat responsive under load.
 */
class ImapMailSyncResponsivenessTest : public QObject
{
    Q_OBJECT

    bool isBackendAvailable()
    {
        QTcpSocket socket;
        socket.connectToHost("localhost", 143);
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

        // Trigger the sync
        Store::synchronize(query).exec();

        //Fetch a message
        //Modify the message
        //Wait for confirmation of the modification (one thing is if we manage to get it into the queue (we currently don't), the other thing is to get it processed.
        
        //I think to get this fixed we have to:
        //* Use threads to decouple event loops
        //* Somehow keep the synchronizer from filling up the eventloop

        // Sink::ResourceAccess resourceAccess(mResourceInstanceIdentifier, "");
        // resourceAccess.open();

        // QTime pingTime;
        // for (int i = 0; i < 500; i++) {
        //     pingTime.start();
        //     VERIFYEXEC(resourceAccess.sendCommand(Sink::Commands::PingCommand));
        //     if (pingTime.elapsed() > 500) {
        //         if (pingTime.elapsed() > 2000) {
        //             SinkWarning() << "Ping took: " << Sink::Log::TraceTime(pingTime.elapsed());
        //             // QVERIFY(pingTime.elapsed() < 2000);
        //         } else {
        //             SinkLog() << "Ping took: " << Sink::Log::TraceTime(pingTime.elapsed());
        //         }
        //     }
        //     QTest::qWait(500);
        // }
        // QTRY_COMPARE(complete, count);

        VERIFYEXEC(ResourceControl::flushMessageQueue(mResourceInstanceIdentifier));
        auto total = time.elapsed();
        SinkLog() << "Total took: " << Sink::Log::TraceTime(total);

        // time.start();

        // VERIFYEXEC(Store::synchronize(query));
        // auto resync = time.elapsed();
        // SinkLog() << "ReSync took: " << Sink::Log::TraceTime(resync);

        // VERIFYEXEC(ResourceControl::flushMessageQueue(QByteArrayList() << mResourceInstanceIdentifier));
        // auto resynctotal = time.elapsed();
        // SinkLog() << "Total resync took: " << Sink::Log::TraceTime(resynctotal);
    }
};

QTEST_MAIN(ImapMailSyncResponsivenessTest)

#include "imapmailsyncresponsivenesstest.moc"
