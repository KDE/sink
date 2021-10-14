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
#include <QTest>
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
#include "common/notification.h"

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

    void testResponsivenessDuringSync()
    {
        Sink::Query query;
        query.resourceFilter(mResourceInstanceIdentifier);

        QTime time;
        time.start();

        // Trigger the sync
        Store::synchronize(query).exec();

        //Repeatedly ping the resource and check if a response arrives within an acceptable timeframe
        //We could improve this check by actually modifying something (it should get priority over the sync)
        Sink::ResourceAccess resourceAccess(mResourceInstanceIdentifier, "");
        resourceAccess.open();

        auto flush = ResourceControl::flushMessageQueue(mResourceInstanceIdentifier).exec();

        const int roundtripSoftLimit = 500;
        const int roundtripHardLimit = 2000;

        QTime pingTime;
        for (int i = 0; i < 500; i++) {
            pingTime.start();
            VERIFYEXEC(resourceAccess.sendCommand(Sink::Commands::PingCommand));
            SinkWarning() << "Ping took: " << Sink::Log::TraceTime(pingTime.elapsed());
            if (pingTime.elapsed() > roundtripSoftLimit) {
                if (pingTime.elapsed() > roundtripHardLimit) {
                    SinkError() << "Ping took: " << Sink::Log::TraceTime(pingTime.elapsed());
                    QVERIFY(false);
                } else {
                    SinkWarning() << "Ping took: " << Sink::Log::TraceTime(pingTime.elapsed());
                }
            }
            //Until the sync is complete
            if (flush.isFinished()) {
                break;
            }
            QTest::qWait(500);
        }

        auto total = time.elapsed();
        SinkLog() << "Total took: " << Sink::Log::TraceTime(total);
    }
};

QTEST_MAIN(ImapMailSyncResponsivenessTest)

#include "imapmailsyncresponsivenesstest.moc"
