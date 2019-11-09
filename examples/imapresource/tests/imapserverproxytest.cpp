#include <QtTest>

#include <QString>
#include <KMime/Message>
#include <QTcpSocket>

#include "../imapserverproxy.h"

#include "log.h"
#include "test.h"
#include "tests/testutils.h"

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

    void testFetchFoldersFailure()
    {
        ImapServerProxy imap("foobar", 143, Imap::EncryptionMode::NoEncryption);
        VERIFYEXEC_FAIL(imap.fetchFolders([](const Folder &){}));
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

    /*
     * Ensure that commands fail and don't just block.
     *
     * Running multiple failing commands one after the other is also covered by this.
     * (We used to have a bug failing under this condition only)
     */
    void testFailures()
    {
        ImapServerProxy imap("foobar", 143, Imap::EncryptionMode::NoEncryption);

        VERIFYEXEC_FAIL(imap.select("INBOX.test"));
        VERIFYEXEC_FAIL(imap.examine("INBOX.test"));
    }

};

QTEST_MAIN(ImapServerProxyTest)
#include "imapserverproxytest.moc"
