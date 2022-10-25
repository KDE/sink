#include <QTest>

#include <QString>
#include <KMime/Message>
#include <QTcpSocket>

#include "../imapserverproxy.h"

#include "log.h"
#include "test.h"

using namespace Imap;

/**
 */
class ImapServerProxyTest : public QObject
{
    Q_OBJECT

    QTemporaryDir tempDir;
    QString targetPath;
private slots:
    void initTestCase()
    {
        Sink::Test::initTest();
        QTcpSocket socket;
        socket.connectToHost("localhost", 143);
        QVERIFY(socket.waitForConnected(200));
        system("resetmailbox.sh");
    }

    void cleanup()
    {
    }

    void init()
    {
        qDebug();
        qDebug() << "-----------------------------------------";
        qDebug();
    }

    void testLogin()
    {
        ImapServerProxy imap("localhost", 143, Imap::EncryptionMode::NoEncryption);
        VERIFYEXEC(imap.login("doe", "doe"));
    }

    void testLoginFailure()
    {
        //Using a bogus ip instead of a bogus hostname avoids getting stuck in the hostname lookup
        ImapServerProxy imap("111.111.1.1", 143, Imap::EncryptionMode::NoEncryption);
        VERIFYEXEC_FAIL(imap.login("doe", "doe"));
    }

    void testFetchFolders()
    {
        QMap<QString, QString> expectedFolderAndParent {
            {"INBOX", ""},
            {"Drafts", ""},
            {"Trash", ""},
            {"test", ""}
        };
        ImapServerProxy imap("localhost", 143, Imap::EncryptionMode::NoEncryption);
        VERIFYEXEC(imap.login("doe", "doe"));
        QVector<Folder> list;
        VERIFYEXEC(imap.fetchFolders([&](const Folder &f){ list << f;}));
        for (const auto &f : list) {
            QVERIFY2(expectedFolderAndParent.contains(f.name()), QString{"Didn't expect folder %1"}.arg(f.name()).toUtf8());
            QCOMPARE(expectedFolderAndParent.value(f.name()), f.parentPath());
            expectedFolderAndParent.remove(f.name());
        }
        QVERIFY(expectedFolderAndParent.isEmpty());
//examples/imapresource/tests/imapserverproxytest testFetchFolders
    }

    void testAppendMail()
    {
        ImapServerProxy imap("localhost", 143, Imap::EncryptionMode::NoEncryption);
        VERIFYEXEC(imap.login("doe", "doe"));


        auto mail = KMime::Message::Ptr::create();
        mail->from(true)->from7BitString("<doe@example.org>");
        mail->to(true)->from7BitString("<doe@example.org>");
        mail->subject(true)->from7BitString("subject");
        mail->setBody("Body");
        auto content = mail->encodedContent(true);

        KIMAP2::MessageFlags flags;
        flags << Imap::Flags::Seen;
        flags << Imap::Flags::Flagged;
        VERIFYEXEC(imap.append("INBOX.test", content, flags, QDateTime::currentDateTimeUtc()));
    }

    void testFetchMail()
    {
        ImapServerProxy imap("localhost", 143, Imap::EncryptionMode::NoEncryption);
        VERIFYEXEC(imap.login("doe", "doe"));

        KIMAP2::FetchJob::FetchScope scope;
        scope.mode = KIMAP2::FetchJob::FetchScope::Headers;
        int count = 0;
        auto job = imap.select("INBOX.test").then<void>(imap.fetch(KIMAP2::ImapSet::fromImapSequenceSet("1:*"), scope,
                    [&count](const KIMAP2::FetchJob::Result &) {
                        count++;
                    }));

        VERIFYEXEC(job);
        QCOMPARE(count, 1);
    }

    void testRemoveMail()
    {
        ImapServerProxy imap("localhost", 143, Imap::EncryptionMode::NoEncryption);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.remove("INBOX.test", "1:*"));

        KIMAP2::FetchJob::FetchScope scope;
        scope.mode = KIMAP2::FetchJob::FetchScope::Headers;
        int count = 0;
        auto job = imap.select("INBOX.test").then<void>(imap.fetch(KIMAP2::ImapSet::fromImapSequenceSet("1:*"), scope,
                    [&count](const KIMAP2::FetchJob::Result &) {
                        count++;
                    }));

        VERIFYEXEC(job);
        QCOMPARE(count, 0);
    }

    void testSessionCache()
    {
        Imap::SessionCache sessionCache;
        {
            //Using a bogus ip instead of a bogus hostname avoids getting stuck in the hostname lookup
            ImapServerProxy imap("111.111.1.1", 143, Imap::EncryptionMode::NoEncryption);
            VERIFYEXEC_FAIL(imap.login("doe", "doe"));
            VERIFYEXEC(imap.logout());
            QCOMPARE(sessionCache.size(), 0);
        }
        {
            ImapServerProxy imap("localhost", 143, Imap::EncryptionMode::NoEncryption, Imap::AuthenticationMode::Plain, &sessionCache);
            VERIFYEXEC(imap.login("doe", "doe"));
            QCOMPARE(sessionCache.size(), 0);
            VERIFYEXEC(imap.logout());
            QCOMPARE(sessionCache.size(), 1);

            auto cachedSession = sessionCache.getSession();
            QCOMPARE(cachedSession.isExpired(), false);
            QCOMPARE(cachedSession.isConnected(), true);

            cachedSession.mSession->close();
            QTest::qWait(1000);
            QCOMPARE(cachedSession.isConnected(), false);

            //TODO this timeout depends on Imap::CachedSession::mTimer
            QTest::qWait(30000);
            QCOMPARE(cachedSession.isExpired(), true);
        }
    }

    //TODO Find a way to deal with the below error:
    // A000029 NO Server ( s ) unavailable to complete operation .\n Sent command: EXAMINE\"INBOX\" (CONDSTORE)
    // We unfortunately don't really have a way to distinguish transient vs. terminal errors, but I suppose we shouldn't normally run into NO responses at all,
    // so perhaps just closing the socket makes sense?
    void testExamine()
    {
        Imap::SessionCache sessionCache;
        ImapServerProxy imap("localhost", 143, Imap::EncryptionMode::NoEncryption, Imap::AuthenticationMode::Plain, &sessionCache);
        VERIFYEXEC(imap.login("doe", "doe"));

        VERIFYEXEC(imap.examine("INBOX"));
        VERIFYEXEC_FAIL(imap.examine("INBOX.failure"));

        VERIFYEXEC(imap.examine("INBOX"));
    }
};

QTEST_MAIN(ImapServerProxyTest)
#include "imapserverproxytest.moc"
