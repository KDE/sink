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
        socket.connectToHost("localhost", 993);
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
        ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));
    }

    void testLoginFailure()
    {
        //Using a bogus ip instead of a bogus hostname avoids getting stuck in the hostname lookup
        ImapServerProxy imap("111.111.1.1", 993);
        VERIFYEXEC_FAIL(imap.login("doe", "doe"));
    }

    void testFetchFolders()
    {
        QMap<QString, QString> expectedFolderAndParent;
        expectedFolderAndParent.insert("INBOX", "");
        expectedFolderAndParent.insert("Drafts", "");
        expectedFolderAndParent.insert("Trash", "");
        expectedFolderAndParent.insert("test", "");
        ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));
        QVector<Folder> list;
        VERIFYEXEC(imap.fetchFolders([&](const Folder &f){ list << f;}));
        for (const auto &f : list) {
            QVERIFY(expectedFolderAndParent.contains(f.name()));
            QCOMPARE(expectedFolderAndParent.value(f.name()), f.parentPath());
            expectedFolderAndParent.remove(f.name());
        }
        QVERIFY(expectedFolderAndParent.isEmpty());
    }

    void testFetchFoldersFailure()
    {
        ImapServerProxy imap("foobar", 993);
        VERIFYEXEC_FAIL(imap.fetchFolders([](const Folder &){}));
    }

    void testFetchMail()
    {
        ImapServerProxy imap("localhost", 993);
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
        ImapServerProxy imap("localhost", 993);
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

};

QTEST_MAIN(ImapServerProxyTest)
#include "imapserverproxytest.moc"
