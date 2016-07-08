#include <QtTest>

#include <QString>
#include <KMime/Message>
#include <QTcpSocket>

#include "../imapserverproxy.h"

#include "log.h"
#include "test.h"
#include "tests/testutils.h"

using namespace Imap;

SINK_DEBUG_AREA("imapserverproxytest")

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
        ImapServerProxy imap("foobar", 993);
        VERIFYEXEC_FAIL(imap.login("doe", "doe"));
    }

    void testFetchFolders()
    {
        ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.fetchFolders([](const QVector<Folder> &){}));
    }

    void testFetchFoldersFailure()
    {
        ImapServerProxy imap("foobar", 993);
        VERIFYEXEC_FAIL(imap.fetchFolders([](const QVector<Folder> &){}));
    }

    void testFetchMail()
    {
        ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));

        KIMAP::FetchJob::FetchScope scope;
        scope.mode = KIMAP::FetchJob::FetchScope::Headers;
        int count = 0;
        auto job = imap.select("INBOX.test").then<void>(imap.fetch(KIMAP::ImapSet::fromImapSequenceSet("1:*"), scope,
                    [&count](const QString &mailbox,
                            const QMap<qint64,qint64> &uids,
                            const QMap<qint64,qint64> &sizes,
                            const QMap<qint64,KIMAP::MessageAttribute> &attrs,
                            const QMap<qint64,KIMAP::MessageFlags> &flags,
                            const QMap<qint64,KIMAP::MessagePtr> &messages) {
                        SinkTrace() << "Received " << uids.size() << " messages from " << mailbox;
                        SinkTrace() << uids.size() << sizes.size() << attrs.size() << flags.size() << messages.size();
                        count += uids.size();
                    }));

        VERIFYEXEC(job);
        QCOMPARE(count, 1);
    }

    void testRemoveMail()
    {
        ImapServerProxy imap("localhost", 993);
        VERIFYEXEC(imap.login("doe", "doe"));
        VERIFYEXEC(imap.remove("INBOX.test", "1:*"));

        KIMAP::FetchJob::FetchScope scope;
        scope.mode = KIMAP::FetchJob::FetchScope::Headers;
        int count = 0;
        auto job = imap.select("INBOX.test").then<void>(imap.fetch(KIMAP::ImapSet::fromImapSequenceSet("1:*"), scope,
                    [&count](const QString &mailbox,
                            const QMap<qint64,qint64> &uids,
                            const QMap<qint64,qint64> &sizes,
                            const QMap<qint64,KIMAP::MessageAttribute> &attrs,
                            const QMap<qint64,KIMAP::MessageFlags> &flags,
                            const QMap<qint64,KIMAP::MessagePtr> &messages) {
                        SinkTrace() << "Received " << uids.size() << " messages from " << mailbox;
                        SinkTrace() << uids.size() << sizes.size() << attrs.size() << flags.size() << messages.size();
                        count += uids.size();
                    }));

        VERIFYEXEC(job);
        QCOMPARE(count, 0);
    }

};

QTEST_MAIN(ImapServerProxyTest)
#include "imapserverproxytest.moc"
